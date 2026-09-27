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

#include "report_outbox.h"
#include <algorithm>
#include <utility>

namespace scorbit {
namespace detail {
namespace achievements {

void ReportOutbox::setSessionUuid(std::string sessionUuid)
{
    m_sessionUuid = std::move(sessionUuid);
}

void ReportOutbox::add(const std::string &userId, std::vector<ReportItem> items)
{
    if (items.empty()) {
        return;
    }
    auto &queue = m_pending[userId];
    for (auto &item : items) {
        auto key = item.key;
        queue.insert_or_assign(std::move(key), std::move(item));
    }
}

std::optional<ReportRequest> ReportOutbox::next()
{
    if (m_inFlight || m_backingOff || m_sessionUuid.empty() || m_pending.empty()) {
        return std::nullopt;
    }

    auto userIt = m_pending.begin();
    ReportRequest request {userIt->first, m_sessionUuid, ++m_sequence, {}};
    for (auto &[key, item] : userIt->second) {
        request.items.push_back(std::move(item));
    }
    m_pending.erase(userIt);

    m_inFlight = request;
    return request;
}

void ReportOutbox::delivered()
{
    m_inFlight.reset();
    m_backoff = INITIAL_BACKOFF;
}

std::chrono::milliseconds ReportOutbox::failed(bool retryable)
{
    if (!m_inFlight) {
        return std::chrono::milliseconds::zero();
    }

    if (!retryable) {
        m_inFlight.reset();
        return std::chrono::milliseconds::zero();
    }

    requeue(*m_inFlight);
    m_inFlight.reset();
    m_backingOff = true;

    const auto delay = m_backoff;
    m_backoff = std::min(m_backoff * 2, MAX_BACKOFF);
    return delay;
}

void ReportOutbox::requeue(const ReportRequest &request)
{
    auto &queue = m_pending[request.userId];
    for (const auto &item : request.items) {
        // An item queued since the request was sent is newer: keep it
        queue.try_emplace(item.key, item);
    }
}

} // namespace achievements
} // namespace detail
} // namespace scorbit
