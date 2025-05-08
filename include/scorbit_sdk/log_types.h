/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include "log_types_c.h"
#include <string>
#include <functional>

namespace scorbit {

/**
 * @enum LogLevel
 * @brief Defines the log levels for the logger.
 */
enum class LogLevel {
    /** @brief Debug log level */
    Debug = SB_DEBUG,

    /** @brief Info log level */
    Info = SB_INFO,

    /** @brief Warning log level */
    Warn = SB_WARN,

    /** @brief Error log level */
    Error = SB_ERROR,
};


/**
 * @typedef LoggerCallback
 * @brief Defines the type for logger callback functions.
 *
 * A callback function allows clients to provide custom logging logic. It is called
 * for each log message with details such as the message content, log level, source file,
 * and line number.
 *
 * The callback function parameters are:
 * - **message**: The content of the log message.
 * - **level**: The level of the log message, represented by @ref scorbit::LogLevel.
 * - **file**: The name of the source file where the log message originated.
 * - **line**: The line number in the source file where the log message originated.
 * - **userData**: A pointer to user-defined data, passed when registering the callback. This
 *                 can be used to maintain context or state within the callback.
 */
using LoggerCallback = std::function<void(const std::string &message, LogLevel level,
                                          const char *file, int line)>;

} // namespace scorbit
