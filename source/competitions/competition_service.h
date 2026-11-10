/*
 * Scorbit SDK
 *
 * (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include "message_outbox.h"
#include "messages.h"
#include "../net_base.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {
namespace competitions {

/** This run's boot id: a random uuid, generated once per process and never stored. */
const std::string &bootId();

/** Where a message to the API stands. */
enum class Delivery {
    Pending,   ///< Not answered yet: it is sent, and sent again, until it is
    Saved,     ///< The server saved it
    Duplicate, ///< The server had saved it already
};

/** One installed attempt, held in memory: a restart ends it (SB-6369). */
struct AttemptRecord {
    CompetitionGoal goal;
    /** When the goal was installed here, device UTC: `installed_at`. */
    int64_t installedAtMs {0};
    Delivery installed {Delivery::Pending};

    /** Set once, when the attempt's game started; never replaced. */
    std::optional<AttemptStart> start;
    Delivery started {Delivery::Pending};
};

/**
 * The machine side of a competition attempt (SB-6437, ADR-0052): installs the goals the API
 * sends, records the game start of each attempt once, and resends both to the API until it
 * answers.
 *
 * Nothing is written to disk (decided 2026-11-09): a restart or power loss ends any attempt,
 * which never resumes. The boot id on each start tells the API a restart cut one off.
 *
 * - A goal is held in memory, then `goal_installed` is sent. A repeated delivery of an attempt
 *   changes nothing; the same attempt with another player or revision is refused.
 * - An attempt starts in the first game that began after it was installed, not one the host found
 *   already running (GameStartOrigin::AlreadyRunning), with a synchronised
 *   clock, in which its player holds a slot of the positions the API accepts (1 to 6). Who holds
 *   which slot is known from the game's own session replies: the create reply, then any update
 *   that seats a player later. The machine-wide profiles are never used, as a late reply for
 *   another session rewrites them.
 * - The start (the game's original UTC, this boot's id and a device event ordinal taken at the
 *   game start, counted from 1 each boot) is kept until confirmed, and sent once the session
 *   uuid is known.
 * - Only a receipt or a 409 answers a message. Anything else is resent. A 409 ends the attempt
 *   for the server, so it is forgotten here.
 *
 * Threading: every method runs on the thread game state lives on. Network replies and timers are
 * brought back to it through the poster.
 */
class CompetitionService
{
public:
    using Poster = std::function<void(std::function<void()>)>;
    using WallClock = std::function<int64_t()>;

    CompetitionService(NetBase &net, Poster poster, WallClock wallClock,
                       std::string bootId = competitions::bootId());
    ~CompetitionService();

    CompetitionService(const CompetitionService &) = delete;
    CompetitionService &operator=(const CompetitionService &) = delete;

    /** A `competition.goal` arrived and parsed. Raises CompetitionGoalReceived unless repeated. */
    void onGoal(CompetitionGoal goal);

    /**
     * Game @p gameSessionId (game state's own id) started at @p startedAtMs, device UTC. When
     * @p alreadyRunning, the host found it in progress (GameStartOrigin::AlreadyRunning): no
     * attempt starts in it.
     */
    void onGameStarted(int gameSessionId, int64_t startedAtMs, bool alreadyRunning = false);

    /** The server created game @p gameSessionId's session and assigned it @p sessionUuid. */
    void onSessionCreated(int gameSessionId, const std::string &sessionUuid);

    /** A reply for session @p sessionUuid named who holds which of its slots. */
    void onSessionPlayers(const std::string &sessionUuid, SessionSeats seats);

    /** Game @p gameSessionId ended. */
    void onGameFinished(int gameSessionId);

    /** The attempts installed here; for tests and diagnostics. */
    const std::map<std::string, AttemptRecord> &attempts() const { return m_attempts; }

private:
    /** A game in which installed attempts may start. */
    struct Game {
        int id {0};
        int64_t startedAtMs {0};
        /** The device event ordinal of the game start, shared by every attempt it starts. */
        int64_t sequence {0};
        std::string sessionUuid;
        bool finished {false};
        /** The attempts installed before the game started, and not started yet. */
        std::vector<std::string> candidates;
    };

    /** Games, and seats waiting for their game, kept at most. */
    static constexpr size_t MAX_KEPT_GAMES = 8;

    Game *gameOf(int gameSessionId);
    /** Starts every candidate of @p game whose player holds one of @p seats. */
    void matchPlayers(Game &game, const SessionSeats &seats);
    void dropSettledGames();

    void refuse(const CompetitionGoal &goal, const std::string &reason);
    void publishGoal(const CompetitionGoal &goal, bool installed, const std::string &reason);

    void queueInstalled(const AttemptRecord &record);
    void queueStarted(const AttemptRecord &record);
    void pump();
    void onReply(const OutgoingMessage &message, ApiReply reply);
    void onRetryTimer();

    NetBase &m_net;
    Poster m_poster;
    WallClock m_wallClock;
    std::string m_bootId;
    /** The last device event ordinal used this boot. */
    int64_t m_lastSequence {0};

    /** attempt → record. */
    std::map<std::string, AttemptRecord> m_attempts;
    std::vector<Game> m_games;
    /** Seats of a session whose game doesn't know its uuid yet, oldest first. */
    std::deque<std::pair<std::string, SessionSeats>> m_unboundSeats;
    MessageOutbox m_outbox;

    /** Guards the service against replies arriving after it was destroyed. */
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace competitions
} // namespace detail
} // namespace scorbit
