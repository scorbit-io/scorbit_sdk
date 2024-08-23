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

/**
 * @enum LogLevel
 * @brief Defines the log levels for the logger.
 */

enum class LogLevel {
    /** @brief Debug log level */
    Debug,

    /** @brief Info log level */
    Info,

    /** @brief Warning log level */
    Warn,

    /** @brief Error log level */
    Error,
};

/**
 * @brief Type definition for the logger callback function.
 *
 * This function type is used for logging messages, allowing clients to register a custom
 * logging function that will be called with detailed information about each log event.
 *
 * The callback function takes the following parameters:
 * - **msg**: The log message content.
 * - **level**: The log level, indicating the severity of the message. This is of type
 *              @ref scorbit::LogLevel.
 * - **file**: The name of the source file where the log message was generated.
 * - **line**: The line number in the source file where the log message was generated.
 * - **userData**: A pointer to arbitrary user data, provided by the client when registering
 *                 the callback. This can be used to pass context or state information.
 */
using logger_callback_t = std::function<void(std::string_view msg, LogLevel level, const char *file,
                                             int line, void *userData)>;

} // namespace scorbit
