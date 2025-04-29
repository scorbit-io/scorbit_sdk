/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/log_types.h>
#include <scorbit_sdk/export.h>

#include <fmt/format.h>
#include <mutex>

namespace scorbit {
namespace detail {

class Logger
{
public:
    static Logger *instance();

    void addCallback(LoggerCallback &&callback, void *userData = nullptr);
    void clear();

    void log(const std::string &message, LogLevel level, const char *file, int line) const;

private:
    Logger() = default;

private:
    struct CallbackAndData {
        LoggerCallback callback;
        void *userData {nullptr};
    };
    std::vector<CallbackAndData> m_callbacks;

    mutable std::mutex m_mutex;

    friend Logger *logger();
};

extern Logger *logger();

// Macros to make the logMessage function easier to call with added filename and line number
#define DBG(...) scorbit::detail::logMessage(LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define INF(...) scorbit::detail::logMessage(LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define WRN(...) scorbit::detail::logMessage(LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define ERR(...) scorbit::detail::logMessage(LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)

template<typename... Args>
constexpr inline void logMessage(LogLevel level, const char *file, int line, Args &&...args)
{
    // Instead of full file path, we just want the file name
    Logger::instance()->log(fmt::format(std::forward<Args>(args)...), level,
                            file + std::string_view(file).find_last_of("/\\") + 1, line);
}

} // namespace detail
} // namespace scorbit
