/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "achievement_storage.h"
#include "definition.h"
#include "player_facts.h"
#include "player_tracker.h"
#include "report_outbox.h"
#include "timeline_row.h"
#include "../net_base.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/** An @ref AchievementUpdate for a particular player, as delivered to game code. */
struct AchievementNotification {
    std::string key;
    PlayerNumber player {0};
    std::string userId;
    AchievementStatus status {AchievementStatus::Progress};
    RuleProgressMap rules;
};

/** Read-only snapshot of the achievements state, safe to read from any thread. */
struct AchievementsView {
    std::shared_ptr<const DefinitionSet> definitions;

    struct Player {
        std::string userId;
        std::map<std::string, PlayerTracker::Record> records;
    };
    std::map<PlayerNumber, Player> players;
};

/**
 * The machine side of the achievements platform (§7.6, §7.7, §12): keeps the definitions cache,
 * derives facts from every committed timeline row, tracks each claimed player, and reports.
 *
 * Threading: every method except @ref view runs on one thread, the one game state lives on.
 * Network replies are brought back to it through the poster given at construction.
 */
class AchievementService
{
public:
    /** Runs a function on the service's thread. */
    using Poster = std::function<void(std::function<void()>)>;

    /** Receives every notification for game code. */
    using NotificationSink = std::function<void(AchievementNotification)>;

    AchievementService(NetBase &net, AchievementStorage storage, Poster poster,
                       NotificationSink sink);
    ~AchievementService();

    AchievementService(const AchievementService &) = delete;
    AchievementService &operator=(const AchievementService &) = delete;

    /** Loads the cached definitions and revalidates them with the API. */
    void refreshDefinitions();

    /** Game @p sessionId (game state's own id) started; @p firstRow is its initial state. */
    void onSessionStarted(int sessionId, const TimelineRow &firstRow);

    /**
     * The server created session @p sessionId and assigned it @p sessionUuid. The game may have
     * ended since; its reports are still waiting for the uuid.
     */
    void onSessionCreated(int sessionId, const std::string &sessionUuid);

    /** A row was committed to the session timeline. */
    void onRow(const TimelineRow &row);

    /** The game ended at @p timeMs: final evaluation, flush, then in-session state is dropped. */
    void onSessionFinished(int64_t timeMs);

    /** Player profiles changed: someone may have claimed or left a slot. */
    void onPlayersChanged();

    /** Reports every changed record now instead of waiting for the ball to end. */
    void flushReports();

    /** Downloads the DMD frame bundle if the definitions announce a newer version. */
    void refreshFrames();

    /** The frame for achievement @p key, if the installed bundle has one. */
    std::optional<std::vector<uint8_t>> frame(const std::string &key) const;

    std::shared_ptr<const AchievementsView> view() const;

private:
    struct Claim {
        std::string userId;
        std::unique_ptr<PlayerTracker> tracker;
        bool fetching {false};
    };

    /** Everything belonging to one game; kept until its reports are delivered. */
    struct Session {
        uint64_t id {0};
        int gameSessionId {0};
        SessionFacts facts;
        std::map<PlayerNumber, Claim> claims;
        std::shared_ptr<ReportOutbox> outbox = std::make_shared<ReportOutbox>();
        bool finished {false};
    };

    void applyDefinitions(const std::string &body, const std::string &etag, bool persist);
    void reconcileClaims();
    void fetchBaseline(PlayerNumber player, const std::string &userId);
    void onBaseline(uint64_t sessionId, PlayerNumber player, const std::string &userId,
                    ApiReply reply);

    void evaluate(Session &session, PlayerNumber player, Claim &claim);
    void publish(Session &session, PlayerNumber player, Claim &claim);
    void reportBallEnd(Session &session, PlayerNumber player);

    void pump(const std::shared_ptr<ReportOutbox> &outbox);
    void onReportReply(const std::shared_ptr<ReportOutbox> &outbox, const ReportRequest &request,
                       ApiReply reply);
    void onRetryTimer();
    void dropDrainedSessions();

    Session *current() { return m_current; }
    Session *sessionOf(const std::shared_ptr<ReportOutbox> &outbox);
    void updateView();

    NetBase &m_net;
    AchievementStorage m_storage;
    Poster m_poster;
    NotificationSink m_sink;

    std::shared_ptr<const DefinitionSet> m_definitions = std::make_shared<DefinitionSet>();
    std::string m_etag;

    std::vector<std::unique_ptr<Session>> m_sessions;
    Session *m_current {nullptr};
    uint64_t m_lastSessionId {0};

    bool m_downloadingFrames {false};

    /** Guards the service against replies arriving after it was destroyed. */
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    mutable std::mutex m_viewMutex;
    std::shared_ptr<const AchievementsView> m_view = std::make_shared<AchievementsView>();
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
