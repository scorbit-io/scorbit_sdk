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
    Worker() = default;
    ~Worker();

    void start();
    void stop();

    bool isRunning() const { return m_running; }

    void post(task_t func);
    void postQueue(task_t func);
    void postGameDataQueue(task_t func);
    void postHeartbeatQueue(task_t func);

    void runTimer(std::chrono::steady_clock::duration delay, task_t func);

private:
    void run();

private:
    using asio_strand = boost::asio::strand<boost::asio::io_context::executor_type>;

    std::atomic_bool m_running {false};

    boost::asio::io_context m_ioc;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> m_workGuard {
            boost::asio::make_work_guard(m_ioc)};

    asio_strand m_strand {m_ioc.get_executor()};
    asio_strand m_gameDataStrand {m_ioc.get_executor()};
    asio_strand m_heartbeatStrand {m_ioc.get_executor()};

    boost::thread_group m_threads;

    boost::asio::steady_timer m_heartbeatTimer {m_ioc};
};

} // namespace detail
} // namespace scorbit
