/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Dec 2024
 *
 ****************************************************************************/

#include "worker.h"
#include "logger.h"

namespace scorbit {
namespace detail {

constexpr auto NUM_OF_THREADS = 4;

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
    if (!m_running)
        return;

    m_workGuard.reset();
    m_threads.join_all();
    m_running = false;
}

void Worker::post(std::function<void()> func)
{
    boost::asio::post(m_ioc, std::move(func));
}

void Worker::postQueue(std::function<void()> func)
{
    boost::asio::post(m_strand, std::move(func));
}

void Worker::postGameDataQueue(std::function<void()> func)
{
    boost::asio::post(m_gameDataStrand, std::move(func));
}

void Worker::postHeartbeatQueue(std::function<void()> func)
{
    boost::asio::post(m_heartbeatStrand, std::move(func));
}

void Worker::runTimer(std::chrono::steady_clock::duration delay, std::function<void()> func)
{
    m_heartbeatTimer.expires_after(delay);
    m_heartbeatTimer.async_wait([func = std::move(func)](const boost::system::error_code &ec) {
        if (!ec) {
            func();
        }
    });
}

} // namespace detail
} // namespace scorbit
