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

#include "worker.h"
#include "utils/thread_priority.h"
#include <logger/logger.h>

// Async handlers never block, so they need very few threads. Two rather than one leaves headroom
// if some handler unexpectedly does block: with a single thread that would freeze all timers and
// the websocket at once.
constexpr auto NUM_OF_ASYNC_THREADS = 2;

// Blocking work (synchronous HTTP, crypto, archive extraction) can occupy a thread for a long
// time, so this is where concurrency is actually needed.
constexpr auto NUM_OF_BLOCKING_THREADS = 4;

using namespace scorbit::detail;
using namespace std::chrono_literals;

// Define custom formatter
template<>
struct fmt::formatter<Worker::Timer> : fmt::formatter<std::string_view> {
    auto format(Worker::Timer c, fmt::format_context &ctx) const
    {
        std::string_view name = "unknown";
        switch (c) {
        case Worker::Timer::TokenRefresh:
            name = "TokenRefresh";
            break;
        case Worker::Timer::NfcCheckTag:
            name = "NfcCheckTag";
            break;
        case Worker::Timer::GameData:
            name = "GameData";
            break;
        case Worker::Timer::SessionUpdate:
            name = "SessionUpdate";
            break;
        case Worker::Timer::CentrifugoReconnect:
            name = "CentrifugoReconnect";
            break;
        case Worker::Timer::CentrifugoIdleDisconnect:
            name = "CentrifugoIdleDisconnect";
            break;
        case Worker::Timer::CentrifugoTokenRefresh:
            name = "CentrifugoTokenRefresh";
            break;
        case Worker::Timer::NfcBootReason:
            name = "NfcBootReason";
            break;
        case Worker::Timer::ModeExpiry:
            name = "ModeExpiry";
            break;
        case Worker::Timer::LeaderboardDeferred:
            name = "LeaderboardDeferred";
            break;
        case Worker::Timer::AuthRetry:
            name = "AuthRetry";
            break;
        case Worker::Timer::Count:
            break;
        }
        return fmt::formatter<std::string_view>::format(name, ctx);
    }
};

namespace scorbit {
namespace detail {

Worker::Worker(int threadNiceValue)
    : m_threadNiceValue(threadNiceValue)
    , m_timers {{
              boost::asio::steady_timer {m_ioc},
              boost::asio::steady_timer {m_ioc},
              boost::asio::steady_timer {m_ioc},
              boost::asio::steady_timer {m_ioc},
              boost::asio::steady_timer {m_ioc},
              boost::asio::steady_timer {m_ioc},
              boost::asio::steady_timer {m_ioc},
              boost::asio::steady_timer {m_ioc},
              boost::asio::steady_timer {m_ioc},
              boost::asio::steady_timer {m_ioc},
              boost::asio::steady_timer {m_ioc},
      }}
{
}

Worker::~Worker()
{
    stop();
}

void Worker::start()
{
    if (m_running) {
        DBG("Worker is already running");
        return;
    }

    m_running = true;
    for (int i = 0; i < NUM_OF_ASYNC_THREADS; ++i) {
        m_threads.create_thread([this] {
            applySdkThreadNice(m_threadNiceValue);
            m_ioc.run();
        });
    }
    for (int i = 0; i < NUM_OF_BLOCKING_THREADS; ++i) {
        m_blockingThreads.create_thread([this] {
            applySdkThreadNice(m_threadNiceValue);
            m_blockingIoc.run();
        });
    }
}

void Worker::stop()
{
    if (!m_running)
        return;

    INF("Worker: stopping...");

    stopAllTimers();

    // Drain blocking work first: its reply callbacks arm timers and publish through Centrifugo,
    // both of which live on the async executor, so that one has to outlive it.
    m_blockingWorkGuard.reset();
    m_blockingThreads.join_all();

    m_workGuard.reset();
    m_threads.join_all();

    m_running = false;

    INF("Worker: stopped");
}

void Worker::post(task_t func)
{
    boost::asio::post(m_blockingIoc, std::move(func));
}

void Worker::postQueue(task_t func)
{
    boost::asio::post(m_strand, std::move(func));
}

void Worker::postSessionQueue(task_t func)
{
    boost::asio::post(m_sessionStrand, std::move(func));
}

void Worker::postGameDataQueue(task_t func)
{
    boost::asio::post(centrifugoStrand(), std::move(func));
}

void Worker::postCommitTask(task_t func)
{
    boost::asio::post(m_commitStrand, std::move(func));
}

void Worker::startTimer(Timer timerType, std::chrono::steady_clock::duration delay, task_t func)
{
    auto *timer = getTimer(timerType);
    if (timer == nullptr) {
        return;
    }

    if (delay >= 10s) {
        DBG("Timer {} started", timerType);
    }

    timer->expires_after(delay);
    timer->async_wait([timerType, func = std::move(func)](const boost::system::error_code &ec) {
        if (!ec) {
            func();
        } else if (ec == boost::asio::error::operation_aborted) {
            DBG("Timer {} cancelled", timerType);
        } else {
            ERR("Timer error: {}", ec.to_string());
        }
    });
}

void Worker::stopTimer(Timer timerType)
{
    auto *timer = getTimer(timerType);
    if (timer == nullptr) {
        return;
    }

    DBG("Timer {} stopped", timerType);

    try {
        timer->cancel();
    } catch (const std::exception &e) {
        ERR("Failed to cancel timer {}: {}", timerType, e.what());
        return;
    }
}

auto Worker::getTimer(Timer timerType) -> boost::asio::steady_timer *
{
    const auto i = static_cast<std::size_t>(timerType);
    if (i >= m_timers.size()) {
        return nullptr;
    }
    return &m_timers[i];
}

void Worker::stopAllTimers()
{
    DBG("Stopping all timers...");

    for (std::size_t i = 0; i < m_timers.size(); ++i) {
        try {
            m_timers[i].cancel();
            DBG("Timer {} stopped", static_cast<Timer>(i));
        } catch (const std::exception &e) {
            ERR("Failed to cancel timer {}: {}", static_cast<Timer>(i), e.what());
        }
    }
}

} // namespace detail
} // namespace scorbit
