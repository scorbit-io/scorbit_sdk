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

#include "message_outbox.h"
#include <algorithm>
#include <iterator>
#include <utility>

namespace scorbit {
namespace detail {
namespace competitions {

void MessageOutbox::add(OutgoingMessage message)
{
    if (m_inFlight == message || std::ranges::find(m_queue, message) != m_queue.end()) {
        return;
    }
    m_queue.push_back(std::move(message));
}

void MessageOutbox::addFirst(OutgoingMessage message)
{
    if (m_inFlight == message) {
        return;
    }
    std::erase(m_queue, message);
    m_queue.push_front(std::move(message));
}

std::optional<OutgoingMessage> MessageOutbox::next()
{
    if (m_inFlight || m_backingOff || m_queue.empty()) {
        return std::nullopt;
    }
    m_inFlight = std::move(m_queue.front());
    m_queue.pop_front();
    return m_inFlight;
}

void MessageOutbox::answered()
{
    m_inFlight.reset();
    m_backoff = INITIAL_BACKOFF;
}

std::chrono::milliseconds MessageOutbox::failed()
{
    if (!m_inFlight) {
        return std::chrono::milliseconds::zero();
    }
    // To the back, with the rest of its attempt in order: one the server keeps failing never
    // holds up other attempts, and an attempt's install still goes before its start
    std::deque<OutgoingMessage> others;
    std::deque<OutgoingMessage> attempt {std::move(*m_inFlight)};
    m_inFlight.reset();
    for (auto &message : m_queue) {
        auto &to = message.attempt == attempt.front().attempt ? attempt : others;
        to.push_back(std::move(message));
    }
    std::ranges::move(attempt, std::back_inserter(others));
    m_queue = std::move(others);
    m_backingOff = true;

    const auto delay = m_backoff;
    m_backoff = std::min(m_backoff * 2, MAX_BACKOFF);
    return delay;
}

} // namespace competitions
} // namespace detail
} // namespace scorbit
