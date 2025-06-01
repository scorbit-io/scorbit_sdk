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

void Worker::post(task_t func)
{
    boost::asio::post(m_ioc, std::move(func));
}

void Worker::postQueue(task_t func)
{
    boost::asio::post(m_strand, std::move(func));
}

void Worker::postGameDataQueue(task_t func)
{
    boost::asio::post(m_gameDataStrand, std::move(func));
}

void Worker::postHeartbeatQueue(task_t func)
{
    boost::asio::post(m_heartbeatStrand, std::move(func));
}

void Worker::runTimer(std::chrono::steady_clock::duration delay, task_t func)
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
