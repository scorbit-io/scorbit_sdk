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

void Logger::addCallback(LoggerCallback &&callback, void *userData)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks.emplace_back(CallbackAndData {std::move(callback), userData});
}

void Logger::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbacks.clear();
}

void Logger::log(const std::string &message, LogLevel level, const char *file, int line) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &item : m_callbacks) {
        if (item.callback) {
            if (LIKELY(message.length() <= MAX_LOG_MESSAGE_LENGTH)) {
                item.callback(message, level, file, line, item.userData);
            } else {
                item.callback(cutLongString(message, MAX_LOG_MESSAGE_LENGTH), level, file, line,
                              item.userData);
            }
        }
    }
}

} // namespace detail
} // namespace scorbit
