/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#pragma once

#include "log_c.h"
#include "log_types.h"
#include <scorbit_sdk/export.h>

namespace {

std::vector<scorbit::LoggerCallback *> g_callbacks;

// C-style callback that forwards to the C++ function
void cLogCallback(const char *message, sb_log_level_t level, const char *file, int line,
                  void *user_data)
{
    auto &callback = *static_cast<scorbit::LoggerCallback *>(user_data);
    if (callback) {
        // Forward to C++ callback
        callback(message, static_cast<scorbit::LogLevel>(level), file, line);
    }
}

} // anonymous namespace

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
 *
 * @note The logger function does not need to be thread-safe, as the logging mechanism ensure thread
 * safety internally.
 *
 * @see resetLogger
 */
inline void addLoggerCallback(LoggerCallback &&callback)
{
    // Store callback in the heap
    auto *callbackPtr = new LoggerCallback(std::move(callback));

    // Keep track of it so we can delete it later
    g_callbacks.push_back(callbackPtr);

    // Register with C API
    sb_add_logger_callback(cLogCallback, callbackPtr);
}

/**
 * @brief Clears all previously added logger callbacks.
 *
 * This function removes the logger callback functions that was previously added
 * using @ref addLoggerCallback. After this call, no logger callback will be invoked
 * until a new one is added.
 *
 * @see addLoggerCallback
 */
inline void resetLogger()
{
    sb_reset_logger();

    for (auto *callback : g_callbacks) {
        delete callback;
    }
    g_callbacks.clear();
}

} // namespace scorbit
