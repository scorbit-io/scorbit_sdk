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

#include <blockingconcurrentqueue.h>
#include <chrono>
#include <mutex>
#include <thread>
#include <variant>
#include <vector>

#if defined(__linux__)
#    include <sys/resource.h>
#    include <sys/syscall.h>
#    include <unistd.h>
#elif defined(__APPLE__)
#    include <pthread.h>
#endif

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

    struct LogDispatcherStop {};

    using LogQueueItem = std::variant<LogData, LogDispatcherStop>;

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
        m_queue.enqueue(LogQueueItem {LogDispatcherStop {}});

        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    void addCallback(LoggerCallback &&callback, size_t maxLength)
    {
        std::scoped_lock lock {m_cbMutex};
        m_callbacks.emplace_back(CallbackAndData {std::move(callback), maxLength});
    }

    void clear()
    {
        std::scoped_lock lock {m_cbMutex};
        m_callbacks.clear();
    }

    void log(const std::string &message, LogLevel level, const char *file, int line)
    {
        using namespace std::chrono;
        const auto timestamp =
                duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

        m_queue.enqueue(LogQueueItem {LogData {message, level, file, line, timestamp}});
    }

private:
    CallbackLogger()
        : m_thread(&CallbackLogger::processLogs, this)
    {
    }

    static void lowerThreadPriority()
    {
#if defined(__linux__)
        const pid_t tid = static_cast<pid_t>(syscall(SYS_gettid));
        setpriority(PRIO_PROCESS, static_cast<id_t>(tid), 10);
#elif defined(__APPLE__)
        pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
#endif
    }

    void processLogs()
    {
        lowerThreadPriority();

        for (;;) {
            LogQueueItem item;
            m_queue.wait_dequeue(item);
            if (std::holds_alternative<LogDispatcherStop>(item)) {
                break;
            }

            auto logData = std::move(std::get<LogData>(item));

            std::unique_lock<std::mutex> cbLock {m_cbMutex};
            for (auto cbItem : m_callbacks) {
                cbLock.unlock();

                if (LIKELY(cbItem.callback)) {
                    if (LIKELY(logData.message.length() < cbItem.maxLength)) {
                        cbItem.callback(logData.message, logData.level, logData.file, logData.line,
                                        logData.timestamp);
                    } else {
                        // C strings must be null-terminated, so we cut the message at
                        // maxLength - 1
                        cbItem.callback(cutLongString(logData.message, cbItem.maxLength - 1),
                                        logData.level, logData.file, logData.line,
                                        logData.timestamp);
                    }
                }

                cbLock.lock(); // Lock again for the next callback
            }
        }
    }

private:
    moodycamel::BlockingConcurrentQueue<LogQueueItem> m_queue;
    std::vector<CallbackAndData> m_callbacks;

    std::mutex m_cbMutex;

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
