/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include "log_types_c.h"
#include <scorbit_sdk/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add a logger callback function to be invoked for log messages.
 *
 * This function allows to register a custom logger callback that will be called
 * whenever a log message is generated. The callback provides detailed information about
 * the log event, including the message, log level, source file, line number, and any
 * user-provided data.
 *
 * @param callback The logger callback function to be registered. It should have the signature
 * specified by @ref sb_log_callback_t.
 * @param userData A pointer to user-defined data that will be passed to the logger callback each
 * time it is invoked. If not used it can be set to `NULL`.
 *
 * @note The logger function does not need to be thread-safe, as the logging mechanism ensure thread
 * safety internally.
 *
 * @see sb_reset_logger
 */
SCORBIT_SDK_EXPORT
void sb_add_logger_callback(sb_log_callback_t callback, void *userData);

/**
 * @brief Clears all previously added logger callbacks.
 *
 * This function removes the logger callback functions that was previously added
 * using @ref sb_add_logger_callback. After this call, no logger callback will be invoked
 * until a new one is added.
 *
 * @see sb_add_logger_callback
 */
SCORBIT_SDK_EXPORT
void sb_reset_logger();

#ifdef __cplusplus
}
#endif
