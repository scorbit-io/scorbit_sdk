/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include "logger.h"

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
            item.callback(message, level, file, line, item.userData);
        }
    }
}

} // namespace detail
} // namespace scorbit
