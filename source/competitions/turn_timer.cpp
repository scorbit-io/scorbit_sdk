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

#include "turn_timer.h"
#include <algorithm>
#include <utility>

namespace scorbit {
namespace detail {
namespace competitions {

TurnTimer::TurnTimer(achievements::PlayerNumber player, TimingEvidence evidence)
    : m_player {player}
    , m_evidence {std::move(evidence)}
{
}

void TurnTimer::begin(const achievements::TimelineRow &startRow, bool goalMet)
{
    // One designated game per timer: what came of it, a result or why there is none, is final
    if (m_state != State::Idle) {
        return;
    }
    m_state = State::Running;
    m_startSequence = startRow.sequence;
    m_startUtcMs = startRow.timeMs;
    m_startSynchronised = startRow.clockSynchronised;

    if (!startRow.elapsedMs) {
        interrupt("the game start has no elapsed time");
        return;
    }
    if (startRow.sequence == 0) {
        interrupt("the game start has no place in the row order");
        return;
    }
    m_startElapsedMs = *startRow.elapsedMs;
    m_lastElapsedMs = *startRow.elapsedMs;
    m_nextSequence = startRow.sequence + 1;
    if (startRow.player == m_player) {
        m_openSinceMs = *startRow.elapsedMs;
    }

    // Met before any time passed: complete at zero, on the start row itself
    if (goalMet) {
        complete();
    }
}

void TurnTimer::onRow(const achievements::TimelineRow &row, bool goalMet)
{
    if (m_state != State::Running) {
        return;
    }
    if (!row.elapsedMs) {
        interrupt("a row has no elapsed time");
        return;
    }
    // A lost row could be the one that made the player current, so its time would go uncounted
    if (row.sequence != m_nextSequence) {
        interrupt("a row is missing, repeated or out of order");
        return;
    }
    const auto now = *row.elapsedMs;
    if (now < m_lastElapsedMs) {
        interrupt("the elapsed clock went backwards");
        return;
    }
    m_lastElapsedMs = now;
    ++m_nextSequence;

    // The player change first, at the row's instant: the time up to it is the previous player's
    if (row.player != m_player) {
        closeTurn(now);
    } else if (!m_openSinceMs) {
        m_openSinceMs = now;
    }

    // Then the goal, at that same instant
    if (goalMet) {
        complete();
    }
}

void TurnTimer::complete()
{
    // The completion's UTC is placed from the start's on the elapsed clock: the server checks it
    // against the competition's window and orders equal durations by it, so a clock that drifted
    // or lost its sync since the start must not move it
    const auto now = m_lastElapsedMs;
    m_result = TurnTimeResult {.durationMs = elapsedAt(now),
                               .startSequence = m_startSequence,
                               .completionSequence = m_nextSequence - 1,
                               .startUtcMs = m_startUtcMs,
                               .completionUtcMs = m_startUtcMs + (now - m_startElapsedMs),
                               .utcSynchronised = m_startSynchronised,
                               .evidence = m_evidence};
    closeTurn(now);
    m_state = State::Completed;
}

void TurnTimer::endAtLastRow(bool goalMet)
{
    if (m_state != State::Running) {
        return;
    }
    if (goalMet) {
        complete();
        return;
    }
    closeTurn(m_lastElapsedMs);
    m_state = State::Ended;
}

void TurnTimer::end(int64_t elapsedMs)
{
    if (m_state != State::Running) {
        return;
    }
    if (elapsedMs < m_lastElapsedMs) {
        interrupt("the elapsed clock went backwards");
        return;
    }
    closeTurn(elapsedMs);
    m_state = State::Ended;
}

void TurnTimer::interrupt(std::string reason)
{
    if (m_state != State::Running) {
        return;
    }
    m_openSinceMs.reset();
    m_interruption = std::move(reason);
    m_state = State::Interrupted;
}

int64_t TurnTimer::elapsedAt(int64_t elapsedMs) const
{
    const int64_t open = m_openSinceMs ? std::max<int64_t>(0, elapsedMs - *m_openSinceMs) : 0;
    return m_closedMs + open;
}

void TurnTimer::closeTurn(int64_t elapsedMs)
{
    if (!m_openSinceMs) {
        return;
    }
    m_closedMs += std::max<int64_t>(0, elapsedMs - *m_openSinceMs);
    m_openSinceMs.reset();
}

} // namespace competitions
} // namespace detail
} // namespace scorbit
