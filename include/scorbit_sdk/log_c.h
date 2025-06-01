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
void sb_reset_logger(void);

#ifdef __cplusplus
}
#endif
