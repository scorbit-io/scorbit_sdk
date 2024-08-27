/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/log_types.h>

#include <fmt/format.h>
#include <mutex>

namespace scorbit {

class Logger
{
public:
    void registerLogger(logger_callback_t &&loggerFunction, void *userData = nullptr);
    void unregisterLogger();

    void log(std::string_view message, LogLevel level, const char *file, int line);

private:
    Logger() = default;

private:
    logger_callback_t m_loggerFunction;
    std::mutex m_loggerMutex;
    void *m_userData {nullptr};

    friend Logger *logger();
};

extern Logger *logger();

// Macros to make the logMessage function easier to call with added filename and line number
#define DBG(...) logMessage(LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define INF(...) logMessage(LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define WRN(...) logMessage(LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define ERR(...) logMessage(LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)

template<typename... Args>
constexpr inline void logMessage(LogLevel level, const char *file, int line, Args &&...args)
{
    // Instead of full file path, we just want the file name
    logger()->log(fmt::format(std::forward<Args>(args)...), level,
                  file + std::string_view(file).find_last_of("/\\") + 1, line);
}

} // namespace scorbit
