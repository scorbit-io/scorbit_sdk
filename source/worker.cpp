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
#include <logger/logger.h>
#include <chrono>

using namespace std::chrono_literals;

constexpr auto NUM_OF_THREADS = 4;
constexpr auto SLEEP_BEETWEEN_CHECKING_QUEUE = 50ms;

using namespace scorbit::detail;
using namespace std::chrono_literals;

// Define custom formatter
template<>
struct fmt::formatter<Worker::Timer> : fmt::formatter<std::string_view> {
    auto format(Worker::Timer c, fmt::format_context &ctx) const
    {
        std::string_view name = "unknown";
        switch (c) {
        case Worker::Timer::Heartbeat:
            name = "Heartbeat";
            break;
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
        case Worker::Timer::NfcBootReason:
            name = "NfcBootReason";
            break;
        case Worker::Timer::Count:
            break;
        }
        return fmt::formatter<std::string_view>::format(name, ctx);
    }
};

namespace scorbit {
namespace detail {

Worker::Worker()
    : m_timers {{
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
    for (int i = 0; i < NUM_OF_THREADS; ++i) {
        m_threads.create_thread([this] { m_ioc.run(); });
    }
    m_gameDataConsumer = std::thread([this] { gameDataConsumerLoop(); });
}

void Worker::stop()
{
    if (!m_running)
        return;

    INF("Worker: stopping...");

    stopAllTimers();

    m_running = false;
    if (m_gameDataConsumer.joinable()) {
        m_gameDataConsumer.join();
    }

    m_workGuard.reset();
    m_threads.join_all();

    INF("Worker: stopped");
}

void Worker::post(task_t func)
{
    boost::asio::post(m_ioc, std::move(func));
}

void Worker::postQueue(task_t func)
{
    boost::asio::post(m_strand, std::move(func));
}

void Worker::postSessionQueue(task_t func)
{
    boost::asio::post(m_sessionStrand, std::move(func));
}

void Worker::postCentrifugoMessage(task_t func)
{
    m_gameDataQueue.enqueue(std::move(func));
}

void Worker::gameDataConsumerLoop()
{
    task_t task;
    while (m_running) {
        if (m_gameDataQueue.try_dequeue(task)) {
            boost::asio::post(m_ioc, std::move(task));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(SLEEP_BEETWEEN_CHECKING_QUEUE));
        }
    }

    while (m_gameDataQueue.try_dequeue(task)) {
        boost::asio::post(m_ioc, std::move(task));
    }
}

void Worker::postHeartbeatQueue(task_t func)
{
    boost::asio::post(m_heartbeatStrand, std::move(func));
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
