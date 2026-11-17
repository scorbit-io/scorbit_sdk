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
    m_state = State::Running;
    m_closedMs = 0;
    m_openSinceMs.reset();
    m_lastElapsedMs = 0;
    m_rowsSinceStart = 0;
    m_startRow = startRow;
    m_result.reset();
    m_interruption.reset();

    if (!startRow.elapsedMs) {
        interrupt("the game start has no elapsed time");
        return;
    }
    if (startRow.sequence == 0) {
        interrupt("the game start has no place in the row order");
        return;
    }
    m_lastElapsedMs = *startRow.elapsedMs;
    if (startRow.player == m_player) {
        m_openSinceMs = *startRow.elapsedMs;
    }

    // Met before any time passed: complete at zero, on the start row itself
    if (goalMet) {
        complete(startRow);
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
    if (row.sequence != m_startRow.sequence + m_rowsSinceStart + 1) {
        interrupt("a row is missing, repeated or out of order");
        return;
    }
    const auto now = *row.elapsedMs;
    if (now < m_lastElapsedMs) {
        interrupt("the elapsed clock went backwards");
        return;
    }
    m_lastElapsedMs = now;
    ++m_rowsSinceStart;

    // The player change first, at the row's instant: the time up to it is the previous player's
    if (row.player != m_player) {
        closeTurn(now);
    } else if (!m_openSinceMs) {
        m_openSinceMs = now;
    }

    // Then the goal, at that same instant
    if (goalMet) {
        complete(row);
    }
}

void TurnTimer::complete(const achievements::TimelineRow &row)
{
    const auto now = *row.elapsedMs;
    m_result = TurnTimeResult {.durationMs = elapsedAt(now),
                               .startSequence = m_startRow.sequence,
                               .completionSequence = row.sequence,
                               .startUtcMs = m_startRow.timeMs,
                               .completionUtcMs = row.timeMs,
                               .utcSynchronised = m_startRow.clockSynchronised
                                               && row.clockSynchronised,
                               .evidence = m_evidence};
    closeTurn(now);
    m_state = State::Completed;
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
