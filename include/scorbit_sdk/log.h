/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include "log_types.h"

namespace scorbit {

/**
 * @brief Registers a logger callback function to be invoked for log messages.
 *
 * This function allows the user to register a custom logger callback that will be called
 * whenever a log message is generated. The callback provides detailed information about
 * the log event, including the message, log level, source file, line number, and any
 * user-provided data.
 *
 * @param loggerFunction The logger callback function to be registered. It should have the
 *                       signature specified by @ref scorbit::logger_callback_t "".
 * @param userData A pointer to user-defined data that will be passed to the logger callback
 *                 each time it is invoked. This parameter is optional and defaults to `nullptr`.
 *
 * @note The logger function does not need to be thread-safe, as the logging mechanism ensures
 *       thread safety internally.
 *
 * @warning The logger function can be registered multiple times, but only the most recently
 *          registered callback will be active. Previous registrations will be overwritten.
 *
 * @see unregisterLogger()
 */
extern void registerLogger(logger_callback_t &&loggerFunction, void *userData = nullptr);

/**
 * @brief Unregisters the currently registered logger callback function.
 *
 * This function removes the logger callback function that was previously registered
 * using @ref registerLogger(). After this call, no logger callback will be invoked
 * until a new one is registered.
 *
 * @see registerLogger()
 */
extern void unregisterLogger();

} // namespace scorbit
