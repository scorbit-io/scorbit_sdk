/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include "log_types.h"
#include <scorbit_sdk/export.h>

namespace scorbit {

/**
 * @brief Add a logger callback function to be invoked for log messages.
 *
 * This function allows to register a custom logger callback that will be called
 * whenever a log message is generated. The callback provides detailed information about
 * the log event, including the message, log level, source file, line number, and any
 * user-provided data.
 *
 * @param callback The logger callback function to be registered. It should have the signature
 * specified by @ref LoggerCallback.
 * @param userData A pointer to user-defined data that will be passed to the logger callback each
 * time it is invoked. This parameter is optional and defaults to `nullptr`.
 *
 * @note The logger function does not need to be thread-safe, as the logging mechanism ensure thread
 * safety internally.
 *
 * @see resetLogger
 */
SCORBIT_SDK_EXPORT
void addLoggerCallback(LoggerCallback &&callback, void *userData = nullptr);

/**
 * @brief Clears all previously added logger callbacks.
 *
 * This function removes the logger callback functions that was previously added
 * using @ref addLoggerCallback. After this call, no logger callback will be invoked
 * until a new one is added.
 *
 * @see addLoggerCallback
 */
SCORBIT_SDK_EXPORT
void resetLogger();

} // namespace scorbit
