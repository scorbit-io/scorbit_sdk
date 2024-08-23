/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include "logger.h"

namespace scorbit {

Logger *logger()
{
    static Logger logger;
    return &logger;
}

void Logger::registerLogger(logger_callback_t &&loggerFunction, void *userData)
{
    std::lock_guard<std::mutex> lock(m_loggerMutex);
    m_loggerFunction = std::move(loggerFunction);
    m_userData = userData;
}

void Logger::unregisterLogger()
{
    registerLogger({});
}

void Logger::log(std::string_view message, LogLevel level, const char *file, int line)
{
    if (m_loggerFunction) {
        std::lock_guard<std::mutex> lock(m_loggerMutex);
        m_loggerFunction(message, level, file, line, m_userData);
    }
}

} // namespace scorbit
