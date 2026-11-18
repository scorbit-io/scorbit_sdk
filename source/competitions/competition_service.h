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
#include "turn_timer.h"
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

    /**
     * Set once, from the attempt's own game, when its player met the goal there (SB-6366): the
     * player's turn time to the first row on which every rule held together (SB-6367). Never
     * replaced. Sending it is B02F (SB-6370).
     */
    std::optional<TurnTimeResult> qualification;
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
 * - The start (the game's original UTC, this boot's id and its start row's place in the order of
 *   rows, TimelineRow::sequence) is kept until confirmed, and sent once the session uuid is known.
 *   The qualification's rows are numbered from the same order, so the pair orders a boot's events.
 * - Each game judges the goals of the attempts that may start in it from that game's own facts,
 *   gathered from its first row: nothing from an earlier game, an earlier attempt, or what the
 *   player's profile holds counts (SB-6366). An attempt's position is only known once a session
 *   reply seats its player, possibly after the goal was met, so until then a TurnTimer times it at
 *   every position the API accepts, each fed every row with the goal's verdict there, and once
 *   more on the game's last row when it ends. The attempt qualifies with its own position's
 *   result, once, and only in the game it started in. A lost row, or a lost end, stops the timing:
 *   no result rather than one shorter than was played.
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

    /** @p bootId is @p net's (NetBase::bootId): game state's row order lives as long. */
    CompetitionService(NetBase &net, Poster poster, WallClock wallClock, std::string bootId);
    ~CompetitionService();

    CompetitionService(const CompetitionService &) = delete;
    CompetitionService &operator=(const CompetitionService &) = delete;

    /**
     * A `competition.goal` arrived and parsed. CompetitionGoalReceived is raised once, when the
     * API first confirms `goal_installed`; a refused goal raises nothing.
     */
    void onGoal(CompetitionGoal goal);

    /**
     * Game @p gameSessionId (game state's own id) started, on @p firstRow: its start time, clock
     * and place in the row order, and the first of the rows its goals are judged on.
     */
    void onGameStarted(int gameSessionId, const achievements::TimelineRow &firstRow);

    /** The server created game @p gameSessionId's session and assigned it @p sessionUuid. */
    void onSessionCreated(int gameSessionId, const std::string &sessionUuid);

    /** A reply for session @p sessionUuid named who holds which of its slots. */
    void onSessionPlayers(const std::string &sessionUuid, SessionSeats seats);

    /** A row was committed to the timeline of the game in progress. */
    void onRow(const achievements::TimelineRow &row);

    /**
     * Game @p gameSessionId ended, its end row the one numbered @p endSequence, committed or not.
     * What closes then (a turn, a mode) is judged once more on that row, if it was the last seen.
     */
    void onGameFinished(int gameSessionId, uint64_t endSequence);

    /** The attempts installed here; for tests and diagnostics. */
    const std::map<std::string, AttemptRecord> &attempts() const { return m_attempts; }

private:
    /** A game in which installed attempts may start. */
    struct Game {
        int id {0};
        int64_t startedAtMs {0};
        /**
         * The start row's place in the order of rows (TimelineRow::sequence), sent with every
         * attempt it starts. Unique within the boot, so it also tells the game's starts apart.
         */
        uint64_t startSequence {0};
        /** The start row's TimelineRow::elapsedMs, from which the facts' times are counted. */
        int64_t startElapsedMs {0};
        /** The last row applied: its place in the row order, and its time as the facts have it. */
        uint64_t lastSequence {0};
        int64_t lastTimeMs {0};
        std::string sessionUuid;
        bool finished {false};
        /** The attempts installed before the game started, and not started yet. */
        std::vector<std::string> candidates;

        /** This game's facts, from its first row; gathered only while it may start an attempt. */
        achievements::SessionFacts facts;
        /** The positions the API accepts that have appeared on its rows. */
        std::set<achievements::PlayerNumber> players;
        /**
         * attempt → position → the player's turn time there until the goal is met. Every position
         * until a reply seats the attempt's player, then only theirs; gone once what came of it
         * is final.
         */
        std::map<std::string, std::map<achievements::PlayerNumber, TurnTimer>> timers;
    };

    /** Games, and seats waiting for their game, kept at most. */
    static constexpr size_t MAX_KEPT_GAMES = 8;

    Game *gameOf(int gameSessionId);
    /** Starts every candidate of @p game whose player holds one of @p seats. */
    void matchPlayers(Game &game, const SessionSeats &seats);
    void dropSettledGames();

    /** Applies @p row to @p game's facts and notes its last row and positions. */
    static void apply(Game &game, const achievements::TimelineRow &row);
    /** Whether @p goal holds for @p position on @p game's facts so far. */
    bool metAt(const Game &game, const CompetitionGoal &goal,
               achievements::PlayerNumber position) const;
    /** Whether @p record may still qualify in @p game: not qualified, nor started elsewhere. */
    static bool mayQualifyIn(const Game &game, const AttemptRecord &record);
    /** Whether some timer of @p game still runs for an attempt that may qualify in it. */
    bool judging(const Game &game) const;
    /** Feeds @p row, or the game's end on its last row when null, to @p game's running timers. */
    void advance(Game &game, const achievements::TimelineRow *row);
    /** Ends @p game, whose end row is numbered @p endSequence (0 if unknown). */
    void finishGame(Game &game, uint64_t endSequence);
    /**
     * Once @p attempt's player is seated in @p game, takes what came of its position's timer: the
     * qualification if its goal was met there. Drops the timers once that is final.
     */
    void qualifyFrom(Game &game, const std::string &attempt);

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
