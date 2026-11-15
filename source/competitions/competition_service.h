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
#include "../achievements/player_facts.h"
#include "../achievements/timeline_row.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
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

/**
 * The moment an attempt met its goal (SB-6366): the first row of its game on which every rule of
 * the saved definition held together, for the attempt's own player at its own position.
 */
struct AttemptQualification {
    /** That row's time, device UTC. */
    int64_t qualifiedAtMs {0};
    /** The device event ordinal of that row, from the same count as the game starts. */
    int64_t sequence {0};

    bool operator==(const AttemptQualification &) const = default;
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

    /**
     * Set once, when the attempt's game first met its goal; never replaced. Its report, with the
     * duration of B02C (SB-6367), is sent by B02F (SB-6370).
     */
    std::optional<AttemptQualification> qualification;
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
 * - An attempt starts in the first game that began after it was installed, with a synchronised
 *   clock, in which its player holds a slot of the positions the API accepts (1 to 6) for its
 *   competition (the slot's Entry, `tournament_id`). Who holds which slot, and for what, is known
 *   from the game's own session replies: the create reply, then any update that seats a player
 *   later. The machine-wide profiles are never used, as a late reply for another session rewrites
 *   them.
 * - The start (the game's original UTC, this boot's id and a device event ordinal taken at the
 *   game start, counted from 1 each boot) is kept until confirmed, and sent once the session
 *   uuid is known.
 * - Each game judges the goals of the attempts that may start in it from that game's own facts,
 *   gathered from its first row: nothing from an earlier game, an earlier attempt, or what the
 *   player's profile holds counts (SB-6366). On every row, and once more when the game ends, each
 *   goal is judged for every player in the game, as the attempt's position is only known once a
 *   session reply seats its player, possibly after the goal was met. The first row on which all of
 *   a goal's rules hold together is kept per position; the attempt qualifies at the one for its
 *   position, once.
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

    /**
     * A `competition.goal` arrived and parsed. CompetitionGoalReceived is raised once, when the
     * API first confirms `goal_installed`; a refused goal raises nothing.
     */
    void onGoal(CompetitionGoal goal);

    /** Game @p gameSessionId (game state's own id) started at @p startedAtMs, device UTC. */
    void onGameStarted(int gameSessionId, int64_t startedAtMs);

    /** The server created game @p gameSessionId's session and assigned it @p sessionUuid. */
    void onSessionCreated(int gameSessionId, const std::string &sessionUuid);

    /** A reply for session @p sessionUuid named who holds which of its slots. */
    void onSessionPlayers(const std::string &sessionUuid, SessionSeats seats);

    /** A row was committed to the timeline of the game in progress. */
    void onRow(const achievements::TimelineRow &row);

    /**
     * Game @p gameSessionId ended, at @p finishedAtMs device UTC when known: what closes then (a
     * turn, a mode) is judged once more.
     */
    void onGameFinished(int gameSessionId, std::optional<int64_t> finishedAtMs = std::nullopt);

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

        /** This game's facts, from its first row; gathered only while it may start an attempt. */
        achievements::SessionFacts facts;
        /** The positions that have appeared on its rows. */
        std::set<achievements::PlayerNumber> players;
        /** attempt → position → the first row on which every rule of its goal held there. */
        std::map<std::string, std::map<achievements::PlayerNumber, AttemptQualification>> metAt;
    };

    /** Games, and seats waiting for their game, kept at most. */
    static constexpr size_t MAX_KEPT_GAMES = 8;

    Game *gameOf(int gameSessionId);
    /** Starts every candidate of @p game whose player holds one of @p seats. */
    void matchPlayers(Game &game, const SessionSeats &seats);
    void dropSettledGames();

    /** Judges the goals of @p game's candidates on its latest row, at @p timeMs. */
    void judgeGoals(Game &game, int64_t timeMs);
    /** Qualifies @p attempt, started in @p game, if its goal was met at its position there. */
    void qualify(const Game &game, const std::string &attempt, AttemptRecord &record);

    void refuse(const CompetitionGoal &goal, const std::string &reason);
    /** Tells the host Scorbit confirmed @p goal is installed: CompetitionGoalReceived. */
    void publishGoal(const CompetitionGoal &goal);

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
