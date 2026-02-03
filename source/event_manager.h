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

#include "event_queue.h"
#include <boost/asio.hpp>
#include <atomic>
#include <memory>

namespace scorbit {
namespace detail {

class EventManager : public std::enable_shared_from_this<EventManager>
{
    using asio_strand = boost::asio::strand<boost::asio::io_context::executor_type>;

public:
    EventManager(asio_strand strand, EventCallback &&callback = nullptr)
        : m_strand {std::move(strand)}
        , m_eventCallback {std::move(callback)}
    {
    }

    ~EventManager() { m_stopped = true; }

    auto push(EventPtr event) -> void
    {
        if (!event || m_stopped) {
            return;
        }

        m_events.enqueue(std::move(event));
        scheduleProcessing();
    }

private:
    auto scheduleProcessing() -> void
    {
        // Always post to ensure processing happens
        // The strand serializes execution
        auto self = shared_from_this();

        boost::asio::post(m_strand, [self]() { self->processEvents(); });
    }

    auto processEvents() -> void
    {
        // Process all available events
        while (!m_stopped) {
            auto event = m_events.dequeue();
            if (!event) {
                break; // Queue is empty
            }

            if (m_eventCallback) {
                m_eventCallback(*event);
            }
        }
    }

private:
    EventQueue m_events;
    asio_strand m_strand;
    EventCallback m_eventCallback;
    std::atomic_bool m_stopped {false};
};

} // namespace detail
} // namespace scorbit
