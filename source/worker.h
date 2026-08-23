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
#include <array>
#include <atomic>
#include <chrono>
#include <functional>

namespace scorbit {
namespace detail {

using task_t = std::function<void()>;

class Worker
{
public:
    enum class Timer {
        TokenRefresh,
        NfcCheckTag,
        GameData,
        SessionUpdate,
        CentrifugoReconnect,
        CentrifugoIdleDisconnect,
        NfcBootReason,
        ModeExpiry,
        LeaderboardDeferred,
        AuthRetry,

        // IMPORTANT! This must be last entry!
        Count,
    };

public:
    /// @param threadNiceValue Linux nice passed to setpriority for each worker thread; 0 disables.
    explicit Worker(int threadNiceValue = 0);
    ~Worker();

    /**
     * Work is split across two executors.
     *
     * The async executor runs everything that must stay responsive and never blocks: timers,
     * the Centrifugo websocket, the UDP keepalive and event delivery. The blocking executor
     * runs everything that does block: synchronous HTTP, crypto, TPM access, archive
     * extraction and file I/O. Keeping them apart is what stops a 14s HTTP request from
     * delaying a timer or a socket read, which on a single-core board is the difference
     * between the host's game loop stuttering and not.
     *
     * Strands work over either executor, so the ordering guarantees callers rely on are
     * unchanged; only the thread pool underneath differs.
     */

    void start();
    void stop();

    bool isRunning() const { return m_running; }

    /// Blocking executor, unserialized. For independent work that may block.
    void post(task_t func);
    /// Blocking executor, serialized on the general queue.
    void postQueue(task_t func);
    /// Blocking executor, serialized on the session queue.
    void postSessionQueue(task_t func);
    /// Async executor, serialized on the Centrifugo strand.
    void postGameDataQueue(task_t func);
    /// Async executor, serialized on the commit strand (publishes through Centrifugo).
    void postCommitTask(task_t func);

    void startTimer(Timer timerType, std::chrono::steady_clock::duration delay, task_t func);
    void stopTimer(Timer timerType);

    auto &heartbeatStrand() { return m_heartbeatStrand; }
    auto &centrifugoStrand() { return m_centrifugoStrand; }
    auto &eventsStrand() { return m_eventsStrand; }

private:
    void run();
    auto getTimer(Timer timerType) -> boost::asio::steady_timer *;
    auto stopAllTimers() -> void;

private:
    using asio_strand = boost::asio::strand<boost::asio::io_context::executor_type>;
    using work_guard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

    std::atomic_bool m_running {false};
    int m_threadNiceValue {0};

    // Async work only; handlers here must not block.
    boost::asio::io_context m_ioc;
    // Blocking work. A second io_context rather than boost::asio::thread_pool because the pool
    // offers no thread-start hook, and every SDK thread needs applySdkThreadNice() applied on
    // its own thread.
    boost::asio::io_context m_blockingIoc;

    work_guard m_workGuard {boost::asio::make_work_guard(m_ioc)};
    work_guard m_blockingWorkGuard {boost::asio::make_work_guard(m_blockingIoc)};

    asio_strand m_strand {m_blockingIoc.get_executor()};
    asio_strand m_sessionStrand {m_blockingIoc.get_executor()};
    asio_strand m_heartbeatStrand {m_ioc.get_executor()};
    asio_strand m_centrifugoStrand {m_ioc.get_executor()};
    asio_strand m_eventsStrand {m_ioc.get_executor()};
    asio_strand m_commitStrand {m_ioc.get_executor()};

    boost::thread_group m_threads;
    boost::thread_group m_blockingThreads;

    std::array<boost::asio::steady_timer, static_cast<std::size_t>(Timer::Count)> m_timers;
};

} // namespace detail
} // namespace scorbit
