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

using logger_function_t =
        std::function<void(std::string_view msg, LogLevel level, const char *file, int line)>;

} // namespace scorbit
