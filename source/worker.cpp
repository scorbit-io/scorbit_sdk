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
#include "logger.h"

constexpr auto NUM_OF_THREADS = 4;

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
        }
        return fmt::formatter<std::string_view>::format(name, ctx);
    }
};

namespace scorbit {
namespace detail {

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
}

void Worker::stop()
{
    INF("Stopping worker...");
    if (!m_running)
        return;

    stopAllTimers();
    m_workGuard.reset();
    m_threads.join_all();
    m_running = false;
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

void Worker::postGameDataQueue(task_t func)
{
    boost::asio::post(centrifugoStrand(), std::move(func));
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
    if (m_timers.count(timerType) == 0) {
        m_timers[timerType] = boost::asio::steady_timer {m_ioc};
    }

    return &m_timers[timerType].value();
}

void Worker::stopAllTimers()
{
    DBG("Stopping all timers...");

    for (auto &[timerType, timer] : m_timers) {
        if (timer.has_value()) {
            try {
                timer->cancel();
                DBG("Timer {} stopped", timerType);
            } catch (const std::exception &e) {
                ERR("Failed to cancel timer {}: {}", timerType, e.what());
            }
        }
    }
}

} // namespace detail
} // namespace scorbit
