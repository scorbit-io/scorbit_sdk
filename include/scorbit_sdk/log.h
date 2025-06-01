/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include "log_c.h"
#include "log_types.h"
#include <scorbit_sdk/export.h>

namespace scorbit {

namespace detail {

static std::vector<scorbit::LoggerCallback *> g_callbacks;

// C-style callback that forwards to the C++ function
inline void cLogCallback(const char *message, sb_log_level_t level, const char *file, int line,
                         int64_t timestamp, void *user_data)
{
    auto &callback = *static_cast<scorbit::LoggerCallback *>(user_data);
    if (callback) {
        // Forward to C++ callback
        callback(message, static_cast<scorbit::LogLevel>(level), file, line, timestamp);
    }
}

} // namespace detail

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
    detail::g_callbacks.push_back(callbackPtr);

    // Register with C API
    sb_add_logger_callback(detail::cLogCallback, callbackPtr);
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

    for (auto *callback : detail::g_callbacks) {
        delete callback;
    }
    detail::g_callbacks.clear();
}

} // namespace scorbit
