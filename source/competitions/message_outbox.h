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

#include <chrono>
#include <deque>
#include <optional>
#include <string>

namespace scorbit {
namespace detail {
namespace competitions {

/** Which device message, and so which endpoint. */
enum class MessageKind { GoalInstalled, AttemptStarted };

/** A message for the API, rendered once from the saved record and sent unchanged every time. */
struct OutgoingMessage {
    std::string attempt;
    MessageKind kind {MessageKind::GoalInstalled};
    std::string body;

    bool operator==(const OutgoingMessage &) const = default;
};

/**
 * The competition messages waiting for the API's answer, as a passive state machine, like
 * achievements::ReportOutbox: the owner sends what @ref next returns and reports how it went.
 *
 * - One message is in flight at a time, in the order queued: an attempt's install goes before its
 *   start.
 * - A message is resent unchanged until the server answers it: a receipt, or a refusal, which is
 *   final. A failure backs off from @ref INITIAL_BACKOFF, doubling up to @ref MAX_BACKOFF.
 */
class MessageOutbox
{
public:
    static constexpr std::chrono::milliseconds INITIAL_BACKOFF {1000};
    static constexpr std::chrono::milliseconds MAX_BACKOFF {30000};

    /** Queues @p message, unless the same one is already queued or in flight. */
    void add(OutgoingMessage message);

    /** The next message to send, marked in flight; nullopt if nothing can be sent now. */
    std::optional<OutgoingMessage> next();

    /** The server answered the in-flight message: saved, duplicate, or refused for good. */
    void answered();

    /**
     * The in-flight message got no answer. It goes back to the front of the queue; the owner
     * waits for the returned delay, then calls @ref retryReady.
     */
    std::chrono::milliseconds failed();

    /** The backoff delay returned by @ref failed has elapsed. */
    void retryReady() { m_backingOff = false; }

    bool hasInFlight() const { return m_inFlight.has_value(); }
    bool empty() const { return m_queue.empty() && !m_inFlight; }

private:
    std::deque<OutgoingMessage> m_queue;
    std::optional<OutgoingMessage> m_inFlight;
    bool m_backingOff {false};
    std::chrono::milliseconds m_backoff {INITIAL_BACKOFF};
};

} // namespace competitions
} // namespace detail
} // namespace scorbit
