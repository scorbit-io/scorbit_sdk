/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include "logger.h"

namespace {

constexpr size_t MAX_LOG_MESSAGE_LENGTH = 511;

#if defined(__GNUC__) || defined(__clang__)
#    define LIKELY(x) __builtin_expect(!!(x), 1)
#    define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#    define LIKELY(x) (x)
#    define UNLIKELY(x) (x)
#endif

std::string cutLongString(const std::string &str, size_t maxLength)
{
    auto shortStr = str;
    constexpr const char *ELIDE_STRING = " ... ";
    constexpr std::size_t ELIDE_STRING_LENGTH = std::char_traits<char>::length(ELIDE_STRING);
    const auto halfSize = (maxLength - ELIDE_STRING_LENGTH) / 2;
    if (shortStr.size() > maxLength) {
        shortStr = fmt::format("{}{}{}", shortStr.substr(0, halfSize), ELIDE_STRING,
                               shortStr.substr(shortStr.length() - halfSize));
    }

    return shortStr;
}

} // namespace

namespace scorbit {
namespace detail {

Logger *Logger::instance()
{
    static Logger logger;
    return &logger;
}

Logger::~Logger()
{
    m_stop = true;
    m_queueCV.notify_all();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void Logger::addCallback(LoggerCallback &&callback, void *userData)
{
    std::lock_guard<std::mutex> lock {m_cbMutex};
    m_callbacks.emplace_back(CallbackAndData {std::move(callback), userData});
}

void Logger::clear()
{
    std::lock_guard<std::mutex> lock {m_cbMutex};
    m_callbacks.clear();
}

void Logger::log(const std::string &message, LogLevel level, const char *file, int line)
{
    std::lock_guard<std::mutex> lock {m_queueMutex};
    m_queue.push(LogData {message, level, file, line});
    m_queueCV.notify_one();
}

Logger::Logger()
    : m_thread(&Logger::processLogs, this)
{
}

void Logger::processLogs()
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
                    if (LIKELY(logData.message.length() <= MAX_LOG_MESSAGE_LENGTH)) {
                        item.callback(logData.message, logData.level, logData.file, logData.line);
                    } else {
                        item.callback(cutLongString(logData.message, MAX_LOG_MESSAGE_LENGTH),
                                      logData.level, logData.file, logData.line);
                    }
                }

                cbLock.lock(); // Lock again for the next callback
            }

            queueLock.lock(); // Lock again for the next iteration of m_queue
        }
    }
}

} // namespace detail
} // namespace scorbit
