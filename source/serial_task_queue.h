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

#include "worker.h"
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

namespace scorbit {
namespace detail {

/**
 * Runs tasks keyed by a string one at a time, in submission order.
 *
 * Posting two related requests to the same strand is not enough to keep them in order, because a
 * request can be deferred mid-flight: the auth gate parks a request that arrives before the status
 * allows it and re-posts it at the *tail* of its strand once authentication completes. Anything
 * queued in the meantime — behind a long download on the shared general queue, say — then runs
 * first, and the parked request lands after it.
 *
 * For config updates that inverts the caller's sequence, and the inversion sticks: a report that
 * lands after the withdrawal which was meant to supersede it re-asserts state the caller already
 * retracted, with no later message to correct it.
 *
 * This queue removes the window by never having more than one task in flight per key. The next
 * task is dispatched only once the previous one reports completion, so a parked request cannot be
 * overtaken by a later one for the same key.
 *
 * Thread-safe. @p Dispatch is invoked outside the lock, so it may run the task inline.
 */
class SerialTaskQueue
{
public:
    using Dispatch = std::function<void(task_t)>;

    explicit SerialTaskQueue(Dispatch dispatch)
        : m_dispatch(std::move(dispatch))
    {
    }

    /// Dispatches @p task immediately when @p key is idle, otherwise queues it behind the
    /// in-flight task for that key.
    void submit(const std::string &key, task_t task)
    {
        {
            std::scoped_lock lock(m_mutex);
            auto &slot = m_slots[key];
            if (slot.inFlight) {
                slot.pending.push_back(std::move(task));
                return;
            }
            slot.inFlight = true;
        }
        m_dispatch(std::move(task));
    }

    /**
     * Reports the in-flight task for @p key complete and dispatches the next one, if any.
     *
     * Must be called exactly once per dispatched task, on every completion path — otherwise the
     * key stalls and later tasks for it are never sent.
     */
    void finish(const std::string &key)
    {
        task_t next;
        {
            std::scoped_lock lock(m_mutex);
            const auto it = m_slots.find(key);
            if (it == m_slots.end()) {
                return;
            }
            if (it->second.pending.empty()) {
                m_slots.erase(it);
                return;
            }
            next = std::move(it->second.pending.front());
            it->second.pending.pop_front();
        }
        m_dispatch(std::move(next));
    }

    /// Tasks waiting behind the in-flight one for @p key.
    size_t pendingCount(const std::string &key) const
    {
        std::scoped_lock lock(m_mutex);
        const auto it = m_slots.find(key);
        return it == m_slots.end() ? 0 : it->second.pending.size();
    }

    /// Whether a task for @p key is currently in flight.
    bool isInFlight(const std::string &key) const
    {
        std::scoped_lock lock(m_mutex);
        const auto it = m_slots.find(key);
        return it != m_slots.end() && it->second.inFlight;
    }

private:
    struct Slot {
        bool inFlight {false};
        std::deque<task_t> pending;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Slot> m_slots;
    Dispatch m_dispatch;
};

} // namespace detail
} // namespace scorbit
