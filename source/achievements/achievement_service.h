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
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/** The system clock, in UTC epoch milliseconds. */
int64_t systemWallClock();

/** Which artwork the service keeps in memory, to hand over with an unlock. */
enum class ArtworkKind {
    None,
    Frame, ///< The DMD frame (`frame`, versioned by `frame_version`)
    Image, ///< The icon (`icon_url`, which has no version: a new URL is a new image)
};

/** An achievement the server granted a claimed player, as delivered to game code. */
struct AchievementUnlock {
    std::string key;
    PlayerNumber player {0};
    std::string name;
    std::string description;

    /** The artwork of the configured kind; empty when there is none. */
    std::vector<uint8_t> artwork;
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

    /** Receives every unlock for game code. */
    using UnlockSink = std::function<void(AchievementUnlock)>;

    /** The wall clock, in UTC epoch milliseconds. */
    using WallClock = std::function<int64_t()>;

    AchievementService(NetBase &net, AchievementStorage storage, Poster poster, UnlockSink sink,
                       ArtworkKind artworkKind = ArtworkKind::None,
                       WallClock wallClock = systemWallClock);
    ~AchievementService();

    AchievementService(const AchievementService &) = delete;
    AchievementService &operator=(const AchievementService &) = delete;

    /**
     * Revalidates the cached definitions with the API, with their `ETag`. Done at boot and after
     * every token refresh, so a machine paired after boot, or an achievement published since,
     * needs no reboot; and, as calendar data changes over time, at least daily and after every
     * local midnight (§10.3). A game in progress keeps the definitions it started with; new ones
     * apply from the next game.
     */
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

    /**
     * Brings the artwork held in memory in line with the definitions: downloads, one at a time,
     * each whose version changed or that isn't held yet, and drops those no longer delivered. An
     * artwork that fails is not retried until the next refresh.
     */
    void refreshArtwork();

    /** The artwork held for achievement @p key, if any. */
    std::optional<std::vector<uint8_t>> artwork(const std::string &key) const;

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
        /** The definitions when the game started: one received mid-game waits for the next. */
        std::shared_ptr<const DefinitionFeed> feed;

        const DefinitionSet &definitions() const { return feed->definitions; }

        /** @p player's seat, evaluated with the calendar the game started with. */
        PlayerSession seat(PlayerNumber player) const { return {facts, player, &feed->calendar}; }
        SessionFacts facts;
        std::map<PlayerNumber, Claim> claims;
        std::shared_ptr<ReportOutbox> outbox = std::make_shared<ReportOutbox>();
        bool finished {false};
    };

    void onDefinitions(ApiReply reply);

    /** The definitions in force were just confirmed: schedules the next revalidation. */
    void scheduleRevalidation();
    bool revalidationDue() const;
    int64_t revalidationDueMs() const;

    /** Puts @p response in force if it parses, and caches it when @p persist. */
    bool applyDefinitions(const CachedDefinitions &response, bool persist);
    /** The artwork URL of @p definition and the version it is held at, for the kind kept. */
    std::pair<std::string, std::string> artworkSource(const Definition &definition) const;
    bool needsArtwork(const Definition &definition) const;
    void downloadNextArtwork();

    /** Hands @p unlock to game code, or holds it while its artwork is still to be downloaded. */
    void deliver(AchievementUnlock unlock);
    void releaseUnlocks(const std::string &key);

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
    UnlockSink m_sink;
    ArtworkKind m_artworkKind;

    /** The definitions in force, from the last response that parsed. */
    std::shared_ptr<const DefinitionFeed> m_feed = std::make_shared<DefinitionFeed>();

    /** The validator of @ref m_feed; empty when its response carried none. */
    std::string m_feedEtag;

    std::vector<std::unique_ptr<Session>> m_sessions;
    Session *m_current {nullptr};
    uint64_t m_lastSessionId {0};

    bool m_fetchingDefinitions {false};

    WallClock m_wallClock;

    /** When the definitions in force were last confirmed with the API. */
    std::optional<int64_t> m_validatedMs;

    /** Spreads the machines of one time zone over the minutes after midnight. */
    std::mt19937 m_random {std::random_device {}()};

    struct Artwork {
        std::string version;
        std::vector<uint8_t> data;
    };

    /** The artwork of the kind kept, by achievement key. */
    std::map<std::string, Artwork> m_artwork;

    /**
     * Keys whose artwork this refresh still has to download, or is downloading. One that fails
     * leaves it, so it isn't retried in a loop.
     */
    std::set<std::string> m_artworkToDownload;
    bool m_downloadingArtwork {false};

    /** Unlocks waiting for their artwork, by achievement key, in the order they happened. */
    std::map<std::string, std::vector<AchievementUnlock>> m_heldUnlocks;

    /** Guards the service against replies arriving after it was destroyed. */
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    mutable std::mutex m_viewMutex;
    std::shared_ptr<const AchievementsView> m_view = std::make_shared<AchievementsView>();
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
