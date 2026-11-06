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

#include "time_interval.h"
#include "timeline_row.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/** Highest player number the timeline records (`p1`…`p6`, §4.1). */
constexpr PlayerNumber MAX_TIMELINE_PLAYERS = 6;

/**
 * Something that happened at an identifiable instant inside one ball: a mode activation, a mode
 * completion, a mode stack forming, or an event operation.
 */
struct Occurrence {
    int64_t timeMs {0};
    BallNumber ball {0};

    /** Index of the ball span (@ref PlayerFacts::balls) it happened in; identifies "one ball". */
    size_t span {0};

    /** 1 for a mode occurrence, the added amount for an event operation. */
    int64_t value {1};

    /**
     * When it stopped lasting, exclusive: a mode activation until the mode left `game_modes`, a
     * stack until it broke, and one still lasting until just after the latest row. A completion
     * or an event operation is an instant, so it ends just after its own row.
     */
    int64_t endMs {0};
};

/**
 * One activation of a mode for as long as the mode stays in `game_modes`: unlike a timed
 * activation, it does not close at ball end or on completion, only when the mode leaves or the
 * session ends (§5.5.1).
 */
struct ModeRun {
    int64_t startMs {0};

    /** When the mode left `game_modes` or the session ended; nullopt while it is still running. */
    std::optional<int64_t> endMs;

    BallNumber ball {0};
    size_t span {0};

    /**
     * Points the owning player scored during it, never below zero: up to the closing row, or up
     * to now while it is still running.
     */
    int64_t gain {0};

    /** The highest @ref gain it reached at any row, which a `GE` rule keeps once met. */
    int64_t peakGain {0};

    bool open() const { return !endMs; }
};

/** Something that lasted, inside one ball: a mode activation or a ball. */
struct Duration {
    int64_t ms {0};
    BallNumber ball {0};
    size_t span {0};

    /**
     * What it measured, as an occurrence to place in a window: a timed activation lasts until its
     * mode leaves `game_modes`, though it stops timing earlier (§5.5.2).
     */
    int64_t startMs {0};
    int64_t endMs {0};
};

/** The player's score on one of their own rows. */
struct ScoreSample {
    int64_t timeMs {0};
    int64_t score {0};
};

/**
 * Every fact of §4.4 for one player in one session, stored raw so that any rule can be derived
 * from it. Built incrementally by @ref SessionFacts. Modes are matched by body, what follows the
 * first `:`: `NA{blue}:Kraken` and `MB:Kraken` are the same mode.
 */
class PlayerFacts
{
public:
    /** Activations of @p mode (absent → present in `game_modes`), in order. */
    std::vector<Occurrence> activations(const std::string &mode) const;

    /** Completions of @p mode (appearances in `completed_modes`), in order. */
    std::vector<Occurrence> completions(const std::string &mode) const;

    /** Operations on event register @p name, in order; each carries its added amount. */
    std::vector<Occurrence> eventOps(const std::string &name) const;

    /** Durations of the activations of @p mode; one still running is measured up to now. */
    std::vector<Duration> activationDurations(const std::string &mode) const;

    /** Durations of the balls played; the ball in progress is measured up to now. */
    std::vector<Duration> ballDurations() const;

    /** Time spent at the machine: the sum of this player's turns, not wall clock. */
    int64_t sessionTimeMs() const;

    /** This player's turns, the one in progress lasting until just after the latest row. */
    std::vector<TimeInterval> turns() const;

    /** The player's score on each row of their own turns, in order. */
    const std::vector<ScoreSample> &ownRowScores() const { return m_ownRowScores; }

    /** Every distinct value this player's score column took this session. */
    const std::set<int64_t> &scoreValues() const { return m_scoreValues; }

    /** The player's current score; nullopt if the column never had a value. */
    std::optional<int64_t> latestScore() const { return m_latestScore; }

private:
    friend class SessionFacts;

    /** One activation: timed until its mode leaves, completes or the ball ends. */
    struct Activation {
        int64_t startMs {0};
        int64_t endMs {0};
        bool open {true};
        BallNumber ball {0};
        size_t span {0};

        /** When the mode left `game_modes`, which may be later; open until then. */
        int64_t presenceEndMs {0};
        bool presenceOpen {true};
    };

    struct BallSpan {
        BallNumber ball {0};
        int64_t startMs {0};
        int64_t endMs {0};
        bool open {true};
    };

    int64_t elapsed(int64_t startMs, int64_t endMs, bool open) const;

    /** The exclusive end of something that ended at @p endMs, or is still @p open. */
    int64_t until(int64_t endMs, bool open) const { return open ? m_nowMs + 1 : endMs; }

    std::unordered_map<std::string, std::vector<Activation>> m_activations;
    std::unordered_map<std::string, std::vector<Occurrence>> m_completions;
    std::unordered_map<std::string, std::vector<Occurrence>> m_eventOps;
    std::vector<BallSpan> m_balls;
    std::set<int64_t> m_scoreValues;
    std::optional<int64_t> m_latestScore;

    std::vector<ScoreSample> m_ownRowScores;

    int64_t m_closedTurnsMs {0};
    std::vector<TimeInterval> m_closedTurns;
    std::optional<int64_t> m_openTurnStartMs;
    int64_t m_nowMs {0};
};

/**
 * Derives the facts of §4.4 for every player from the session timeline, live, one row at a time.
 *
 * Attribution and boundaries:
 * - an activation, completion, event operation or stack forming belongs to the row's active
 *   player;
 * - a ball ends when the `ball` column changes, the active player changes, or the session ends;
 *   every activation still open is closed there with its real elapsed duration, and a mode that is
 *   still present afterwards is not a new activation;
 * - modes are compared as sets, and names that are not valid timeline names are ignored;
 * - durations never go negative, even if the wall clock steps backwards.
 */
class SessionFacts
{
public:
    void apply(const TimelineRow &row);

    /** Closes the running turn, ball and activations at @p timeMs: the session is over. */
    void finish(int64_t timeMs);

    /** Facts for @p player; empty facts if the player has not appeared yet. */
    const PlayerFacts &player(PlayerNumber player) const;

    /**
     * Each time all of @p modes became present in `game_modes` together, for the stacks
     * @p player formed: the player active on the row on which the last of them arrived. A stack
     * that stays intact is one occurrence, across ball and player changes; one that breaks and
     * re-forms is two.
     */
    std::vector<Occurrence> stacks(const std::vector<std::string> &modes,
                                   PlayerNumber player) const;

    /** The activations of @p mode that @p player owns, in order, as `MODE_SCORE` sees them. */
    std::vector<ModeRun> modeRuns(const std::string &mode, PlayerNumber player) const;

    /** The time of the latest row, or of the session's end once finished. */
    int64_t nowMs() const { return m_nowMs; }

    /** Whether the latest row's clock was synchronised. */
    bool clockSynchronised() const { return !m_prev || m_prev->clockSynchronised; }

    /**
     * The instants the session's clock vouched for: from each synchronised row to the next, the
     * latest one lasting until just after it.
     */
    IntervalSet synchronisedPeriods() const;

    /** Whether the last applied row ended a ball, and for whom. */
    std::optional<PlayerNumber> lastRowEndedBallOf() const { return m_ballEndedFor; }

    bool finished() const { return m_finished; }

private:
    /** A mode's uninterrupted presence in `game_modes`, in rows: [startRow, endRow). */
    struct PresenceRun {
        size_t startRow {0};
        std::optional<size_t> endRow {};
        int64_t startMs {0};
        int64_t endMs {0};

        /** Active player, ball and ball span on the row the mode arrived: its owner. */
        PlayerNumber player {0};
        BallNumber ball {0};
        size_t span {0};

        /** The owner's score on the row the mode arrived, and on the row that closed the run. */
        int64_t startScore {0};
        int64_t endScore {0};
        int64_t peakGain {0};
    };

    PlayerFacts *facts(PlayerNumber player);
    int64_t scoreOf(PlayerNumber player) const;
    void closeRun(const std::string &mode, int64_t timeMs);
    void closeTurn(PlayerNumber player, int64_t timeMs);
    void vouchFor(const TimelineRow &row, int64_t untilMs);
    void closeBall(int64_t timeMs);
    void openBall(PlayerNumber player, BallNumber ball, int64_t timeMs);
    void closeActivation(const std::string &mode, int64_t timeMs);

    std::map<PlayerNumber, PlayerFacts> m_players;
    std::optional<TimelineRow> m_prev;
    size_t m_rows {0};
    std::set<std::string> m_presentModes;

    /** Every mode's presence runs, whoever activated it. */
    std::unordered_map<std::string, std::vector<PresenceRun>> m_presence;

    /** Mode name → player whose activation of it is open. */
    std::map<std::string, PlayerNumber> m_openActivations;

    std::optional<PlayerNumber> m_ballEndedFor;
    int64_t m_nowMs {0};
    std::vector<TimeInterval> m_synchronisedPeriods;
    bool m_finished {false};
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
