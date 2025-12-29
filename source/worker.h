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

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/thread.hpp>
#include <atomic>
#include <chrono>

namespace scorbit {
namespace detail {

using task_t = std::function<void()>;

class Worker
{
public:
    enum class Timer {
        Heartbeat,
        TokenRefresh,
        NfcCheckTag,
        GameData,
        SessionUpdate,
        CentrifugoReconnect,
    };

public:
    Worker() = default;
    ~Worker();

    void start();
    void stop();

    bool isRunning() const { return m_running; }

    void post(task_t func);
    void postQueue(task_t func);
    void postSessionQueue(task_t func);
    void postGameDataQueue(task_t func);
    void postHeartbeatQueue(task_t func);
    void postCommitTask(task_t func);

    void startTimer(Timer timerType, std::chrono::steady_clock::duration delay, task_t func);
    void stopTimer(Timer timerType);

    auto &centrifugoStrand() { return m_centrifugoStrand; }
    auto &eventsStrand() { return m_eventsStrand; }

private:
    void run();
    auto getTimer(Timer timerType) -> boost::asio::steady_timer *;
    auto stopAllTimers() -> void;

private:
    using asio_strand = boost::asio::strand<boost::asio::io_context::executor_type>;

    std::atomic_bool m_running {false};

    boost::asio::io_context m_ioc;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_workGuard {
            boost::asio::make_work_guard(m_ioc)};

    asio_strand m_strand {m_ioc.get_executor()};
    asio_strand m_sessionStrand {m_ioc.get_executor()};
    asio_strand m_heartbeatStrand {m_ioc.get_executor()};
    asio_strand m_centrifugoStrand {m_ioc.get_executor()};
    asio_strand m_eventsStrand {m_ioc.get_executor()};
    asio_strand m_commitStrand {m_ioc.get_executor()};

    boost::thread_group m_threads;

    std::unordered_map<Timer, std::optional<boost::asio::steady_timer>> m_timers;
};

} // namespace detail
} // namespace scorbit
