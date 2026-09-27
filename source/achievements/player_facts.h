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
};

/** Something that lasted, inside one ball: a mode activation or a ball. */
struct Duration {
    int64_t ms {0};
    BallNumber ball {0};
    size_t span {0};
};

/**
 * Every fact of §4.4 for one player in one session, stored raw so that any rule can be derived
 * from it. Built incrementally by @ref SessionFacts.
 */
class PlayerFacts
{
public:
    /** Activations of @p mode (absent → present in `game_modes`), in order. */
    std::vector<Occurrence> activations(const std::string &mode) const;

    /** Completions of @p mode (appearances in `completed_modes`), in order. */
    std::vector<Occurrence> completions(const std::string &mode) const;

    /** Instants at which all of @p modes became simultaneously active. */
    std::vector<Occurrence> stacks(const std::vector<std::string> &modes) const;

    /** Operations on event register @p name, in order; each carries its added amount. */
    std::vector<Occurrence> eventOps(const std::string &name) const;

    /** Durations of the activations of @p mode; one still running is measured up to now. */
    std::vector<Duration> activationDurations(const std::string &mode) const;

    /** Durations of the balls played; the ball in progress is measured up to now. */
    std::vector<Duration> ballDurations() const;

    /** Time spent at the machine: the sum of this player's turns, not wall clock. */
    int64_t sessionTimeMs() const;

    /** Every value this player's score took, as (min, max, latest); nullopt if never scored. */
    struct Scores {
        int64_t min {0};
        int64_t max {0};
        int64_t latest {0};
    };
    std::optional<Scores> scores() const { return m_scores; }

private:
    friend class SessionFacts;

    struct Activation {
        int64_t startMs {0};
        int64_t endMs {0};
        bool open {true};
        BallNumber ball {0};
        size_t span {0};
    };

    struct BallSpan {
        BallNumber ball {0};
        int64_t startMs {0};
        int64_t endMs {0};
        bool open {true};
    };

    int64_t elapsed(int64_t startMs, int64_t endMs, bool open) const;

    std::unordered_map<std::string, std::vector<Activation>> m_activations;
    std::unordered_map<std::string, std::vector<Occurrence>> m_completions;
    std::unordered_map<std::string, std::vector<Occurrence>> m_eventOps;
    std::vector<BallSpan> m_balls;
    std::optional<Scores> m_scores;

    int64_t m_closedTurnsMs {0};
    std::optional<int64_t> m_openTurnStartMs;
    int64_t m_nowMs {0};
};

/**
 * Derives @ref PlayerFacts for every player from the session timeline, one row at a time, exactly
 * as the API derives them from the uploaded timeline (§4).
 *
 * Attribution and boundaries:
 * - an activation, completion or event operation belongs to the row's active player;
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

    /** Whether the last applied row ended a ball, and for whom. */
    std::optional<PlayerNumber> lastRowEndedBallOf() const { return m_ballEndedFor; }

    bool finished() const { return m_finished; }

private:
    PlayerFacts *facts(PlayerNumber player);
    void closeBall(int64_t timeMs);
    void openBall(PlayerNumber player, BallNumber ball, int64_t timeMs);
    void closeActivation(const std::string &mode, int64_t timeMs);

    std::map<PlayerNumber, PlayerFacts> m_players;
    std::optional<TimelineRow> m_prev;
    std::set<std::string> m_presentModes;

    /** Mode name → player whose activation of it is open. */
    std::map<std::string, PlayerNumber> m_openActivations;

    std::optional<PlayerNumber> m_ballEndedFor;
    bool m_finished {false};
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
