/*
 * Logger Callback Implementation
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include "detail/cut_long_string.h"
#include <logger/logger_callback.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace {

#if defined(__GNUC__) || defined(__clang__)
#    define LIKELY(x) __builtin_expect(!!(x), 1)
#    define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#    define LIKELY(x) (x)
#    define UNLIKELY(x) (x)
#endif

} // anonymous namespace

namespace logger {
namespace detail {

class CallbackLogger
{
    struct LogData {
        std::string message; // Log message
        LogLevel level;      // Log level
        const char *file;    // Source file name
        int line;            // Source line number
        int64_t timestamp;   // Timestamp in milliseconds since epoch
    };

    struct CallbackAndData {
        LoggerCallback callback;
        size_t maxLength {512};
    };

public:
    static CallbackLogger *instance()
    {
        static CallbackLogger logger;
        return &logger;
    }

    ~CallbackLogger()
    {
        m_stop = true;
        m_queueCV.notify_all();

        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    void addCallback(LoggerCallback &&callback, size_t maxLength)
    {
        std::lock_guard<std::mutex> lock {m_cbMutex};
        m_callbacks.emplace_back(CallbackAndData {std::move(callback), maxLength});
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock {m_cbMutex};
        m_callbacks.clear();
    }

    void log(const std::string &message, LogLevel level, const char *file, int line)
    {
        using namespace std::chrono;
        const auto timestamp =
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        std::lock_guard<std::mutex> lock {m_queueMutex};
        m_queue.push(LogData {message, level, file, line, timestamp});
        m_queueCV.notify_one();
    }

private:
    CallbackLogger()
        : m_thread(&CallbackLogger::processLogs, this)
    {
    }

    void processLogs()
    {
        while (!m_stop) {
            std::unique_lock<std::mutex> queueLock {m_queueMutex};
            m_queueCV.wait(queueLock, [this] { return !m_queue.empty() || m_stop; });

            while (!m_queue.empty()) {
                const auto logData = std::move(m_queue.front());
                m_queue.pop();
                queueLock.unlock(); // Unlock before calling callbacks to avoid deadlock

                std::unique_lock<std::mutex> cbLock {m_cbMutex};
                for (auto item : m_callbacks) {
                    cbLock.unlock();

                    if (LIKELY(item.callback)) {
                        if (LIKELY(logData.message.length() < item.maxLength)) {
                            item.callback(logData.message, logData.level, logData.file,
                                          logData.line, logData.timestamp);
                        } else {
                            // C strings must be null-terminated, so we cut the message at
                            // maxLength - 1
                            item.callback(cutLongString(logData.message, item.maxLength - 1),
                                          logData.level, logData.file, logData.line,
                                          logData.timestamp);
                        }
                    }

                    cbLock.lock(); // Lock again for the next callback
                }

                queueLock.lock(); // Lock again for the next iteration of m_queue
            }
        }
    }

private:
    std::atomic_bool m_stop {false};
    std::queue<LogData> m_queue;
    std::vector<CallbackAndData> m_callbacks;

    std::mutex m_cbMutex;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;

    std::thread m_thread; // This should be last, other members must be valid when it is destroyed
};

} // namespace detail

// ---- Implementation of logger::log() (from libs/logger interface) ----

void log(const std::string &message, LogLevel level, const char *file, int line)
{
    detail::CallbackLogger::instance()->log(message, level, file, line);
}

// ---- C++ API ----

void addCallback(LoggerCallback &&callback, size_t maxLength)
{
    detail::CallbackLogger::instance()->addCallback(std::move(callback), maxLength);
}

void resetCallbacks()
{
    detail::CallbackLogger::instance()->clear();
}

} // namespace logger
