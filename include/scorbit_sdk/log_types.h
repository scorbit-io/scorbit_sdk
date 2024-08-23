/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include <string_view>
#include <functional>

namespace scorbit {

enum class LogLevel
{
    Debug,
    Info,
    Warn,
    Error,
};

/**
 * @brief Logger function type
 * @param msg Log message
 * @param level log level (LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error)
 * @param file file name
 * @param line line number
 * @param user pointer to arbitrary user data
 */
using logger_function_t = std::function<void(std::string_view msg, LogLevel level, const char *file,
                                             int line, void *user)>;

} // namespace scorbit
