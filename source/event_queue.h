/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
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

#include "event_classes.h"
#include <queue>
#include <mutex>
#include <memory>

namespace scorbit {
namespace detail {

using EventPtr = std::shared_ptr<EventBase>;

class EventQueue
{
    struct CompareEvents {
        bool operator()(const EventPtr &lhs, const EventPtr &rhs) const
        {
            return *lhs < *rhs; // delegate to EventBase::operator<
        }
    };

public:
    EventQueue() = default;

    auto empty() const -> bool
    {
        std::scoped_lock lock(m_queueMutex);
        return m_queue.empty();
    }

    auto enqueue(EventPtr &&event) -> void
    {
        if (!event) {
            return; // Ignore null events
        }

        std::scoped_lock lock(m_queueMutex);
        m_queue.push(std::move(event));
    }

    auto dequeue() -> EventPtr
    {
        std::scoped_lock lock(m_queueMutex);
        if (m_queue.empty()) {
            return nullptr;
        }

        EventPtr event {m_queue.top()};
        m_queue.pop();
        return event;
    }

private:
    std::priority_queue<EventPtr, std::vector<EventPtr>, CompareEvents> m_queue;
    mutable std::mutex m_queueMutex;
};

} // namespace detail
} // namespace scorbit
