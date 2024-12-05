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

constexpr auto NUM_OF_THREADS = 3;

Worker::Worker()
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
}

void Worker::stop()
{
    if (!m_running)
        return;

    m_workGuard.reset();
    // m_ioc.stop();    // TODO: if we want to abrupt the running tasks?
    m_threads.join_all();
    m_running = false;
}

void Worker::postQueue(std::function<void()> func)
{
    boost::asio::post(m_strand, std::move(func));
}

} // namespace detail
} // namespace scorbit
