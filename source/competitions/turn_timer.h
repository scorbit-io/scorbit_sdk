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

#include "../achievements/timeline_row.h"
#include "../utils/elapsed_clock.h"
#include <compare>
#include <cstdint>
#include <optional>
#include <string>

namespace scorbit {
namespace detail {
namespace competitions {

/**
 * The timing policy a result was measured under. Bump it whenever what counts as the player's time
 * changes, so results measured under different rules are never compared as equals.
 *
 * Version 1: whole current-player turns, from the designated game's start to the row on which the
 * goal is first met, on the elapsed clock of committed timeline rows.
 */
constexpr int TURN_TIMER_POLICY_VERSION = 1;

/** What a timed duration rests on, saved with it (SB-6367 §4). */
struct TimingEvidence {
    int policyVersion {TURN_TIMER_POLICY_VERSION};

    /** Durations are whole milliseconds, good to @ref resolutionMs: no finer than the source. */
    const char *unit {"ms"};
    int64_t resolutionMs {elapsedClockResolutionMs()};
    const char *clockSource {elapsedClockSource()};

    /** The machine that timed it, and the boot its elapsed clock belongs to. */
    std::string deviceId;
    std::string bootId;
};

/** The player's time to first meet the goal in one designated game. */
struct TurnTimeResult {
    /** The player's own turn time from the game's start to the completing row. */
    int64_t durationMs {0};

    /** Ordinals of the game's start row and of the completing row (TimelineRow::sequence). */
    uint64_t startSequence {0};
    uint64_t completionSequence {0};

    /**
     * Device UTC of both rows, for schedule and calendar eligibility only: never used to measure.
     * @ref utcSynchronised tells whether the server vouched for the clock on both.
     */
    int64_t startUtcMs {0};
    int64_t completionUtcMs {0};
    bool utcSynchronised {false};

    TimingEvidence evidence;
};

/**
 * Orders results by duration alone: shorter is faster, and equal durations stay equal, whatever
 * their UTC or score. Results measured under different policies or resolutions are unordered.
 */
inline std::partial_ordering compareDurations(const TurnTimeResult &a, const TurnTimeResult &b)
{
    if (a.evidence.policyVersion != b.evidence.policyVersion
        || a.evidence.resolutionMs != b.evidence.resolutionMs) {
        return std::partial_ordering::unordered;
    }
    return a.durationMs <=> b.durationMs;
}

/**
 * Times one player through one designated game (SB-6367): the sum of the intervals in which that
 * player is current, from the game's start until the first row on which the goal is met.
 *
 * - Whole turns, not ball-in-play time: launch waiting, play, bonus and the gap between balls all
 *   count while the player is current. Another player's turn adds nothing.
 * - Measured on each row's @ref achievements::TimelineRow::elapsedMs, on @ref ElapsedClock: a UTC
 *   correction can neither add nor remove time, and time the machine spends suspended still
 *   counts. UTC is only carried into the result.
 * - A row is one input event, applied whole: a player change takes effect at the row's instant,
 *   then the goal is checked at that same instant. When a switch and a completion share a row, the
 *   time up to the row belongs to whoever was current before it, whichever player completed, so no
 *   order of handlers can change the result.
 * - The first completion is final; later rows can neither lengthen nor replace it.
 * - Timer state lives in memory only. A restart ends the attempt (SB-6369), so elapsed ticks never
 *   cross a boot. Within a boot, any doubt about continuity stops qualification rather than drop
 *   the doubtful time and yield a faster result than was played.
 */
class TurnTimer
{
public:
    enum class State {
        Idle,        ///< No designated game yet
        Running,     ///< Timing the designated game
        Completed,   ///< The goal was met; the result is final
        Ended,       ///< The game ended without meeting the goal
        Interrupted, ///< Continuity was lost before the goal was met: no result
    };

    TurnTimer(achievements::PlayerNumber player, TimingEvidence evidence);

    /**
     * A designated game started, on @p startRow: timing starts again at zero, and the attempt is
     * consumed. Without an elapsed time or a sequence on the row, continuity is unknown from the
     * start. The previous game's result is discarded, so the caller saves it first.
     *
     * @p goalMet if the goal is already satisfied on the start row: the result is then zero,
     * completed on the start row itself. That row is never applied again through @ref onRow.
     */
    void begin(const achievements::TimelineRow &startRow, bool goalMet = false);

    /**
     * A row was committed in the designated game; @p goalMet if the goal is satisfied on it. Every
     * row of the game must be applied, in order: one missing, repeated or out of order stops
     * qualification, since a lost row may be the one that made the player current.
     */
    void onRow(const achievements::TimelineRow &row, bool goalMet);

    /** The designated game ended at @p elapsedMs without meeting the goal. */
    void end(int64_t elapsedMs);

    /**
     * Continuity of the input is no longer known, for @p reason: qualification stops, with no
     * result. A result already saved, and the consumed attempt, are kept.
     */
    void interrupt(std::string reason);

    State state() const { return m_state; }
    achievements::PlayerNumber player() const { return m_player; }

    /** Whether a designated game started, whatever came of it. */
    bool attemptConsumed() const { return m_state != State::Idle; }

    /** The player's time in the turns that have closed. */
    int64_t closedMs() const { return m_closedMs; }

    /** When the player's still-open turn began, on the elapsed clock; nullopt between turns. */
    std::optional<int64_t> openSinceMs() const { return m_openSinceMs; }

    /** The player's time up to @p elapsedMs: closed turns plus the open one so far. */
    int64_t elapsedAt(int64_t elapsedMs) const;

    /** The result, once the goal is met. */
    const std::optional<TurnTimeResult> &result() const { return m_result; }

    /** Why qualification stopped, once interrupted. */
    const std::optional<std::string> &interruption() const { return m_interruption; }

private:
    void closeTurn(int64_t elapsedMs);
    /** The goal is first met on @p row, already applied: the result is saved and final. */
    void complete(const achievements::TimelineRow &row);

    achievements::PlayerNumber m_player;
    TimingEvidence m_evidence;

    State m_state {State::Idle};
    int64_t m_closedMs {0};
    std::optional<int64_t> m_openSinceMs;
    int64_t m_lastElapsedMs {0};
    /** Rows applied after the start row: the next must be numbered one past them. */
    uint64_t m_rowsSinceStart {0};

    achievements::TimelineRow m_startRow;
    std::optional<TurnTimeResult> m_result;
    std::optional<std::string> m_interruption;
};

} // namespace competitions
} // namespace detail
} // namespace scorbit
