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

#include "report.h"
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

/**
 * The queue of reports for one session (§7.7 "Queue and retry"), as a passive state machine: the
 * owner sends what @ref next returns and reports back how it went.
 *
 * - Reports are never dropped for a transport failure. Values are absolute snapshots, so the
 *   latest item per player and achievement supersedes an older one still waiting.
 * - One request is in flight per session. `sequence` is shared by every player of the session
 *   (§10.1), so a retry is never replayed with a stale sequence: a failed request's items go back
 *   to the queue and are sent again in a fresh request with a higher sequence.
 * - Retries back off exponentially, from @ref INITIAL_BACKOFF up to @ref MAX_BACKOFF.
 * - Nothing is sent until the server has assigned the session its uuid.
 */
class ReportOutbox
{
public:
    static constexpr std::chrono::milliseconds INITIAL_BACKOFF {1000};
    static constexpr std::chrono::milliseconds MAX_BACKOFF {30000};

    /** Binds the session uuid the server assigned; reports queue until then. */
    void setSessionUuid(std::string sessionUuid);
    const std::string &sessionUuid() const { return m_sessionUuid; }

    /** Queues @p items for @p userId, superseding older queued items for the same keys. */
    void add(const std::string &userId, std::vector<ReportItem> items);

    /** The next request to send, marked in flight; nullopt if nothing can be sent now. */
    std::optional<ReportRequest> next();

    /** The in-flight request was processed by the server. */
    void delivered();

    /**
     * The in-flight request failed. When @p retryable, its items are queued again and the owner
     * must wait for the returned delay, then call @ref retryReady; otherwise they are dropped.
     */
    std::chrono::milliseconds failed(bool retryable);

    /** The backoff delay returned by @ref failed has elapsed. */
    void retryReady() { m_backingOff = false; }

    bool hasInFlight() const { return m_inFlight.has_value(); }
    bool empty() const { return m_pending.empty() && !m_inFlight; }

private:
    void requeue(const ReportRequest &request);

    std::string m_sessionUuid;
    int64_t m_sequence {0};

    /** userId → key → item, waiting to be sent. */
    std::map<std::string, std::map<std::string, ReportItem>> m_pending;

    std::optional<ReportRequest> m_inFlight;
    bool m_backingOff {false};
    std::chrono::milliseconds m_backoff {INITIAL_BACKOFF};
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
