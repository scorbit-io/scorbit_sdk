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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum sb_log_level_t
 * @brief Defines the log levels for the logger.
 */
// Implementation warning: make sure to sync with corresponding LogLevel!
typedef enum {
    /** @brief Debug log level */
    SB_DEBUG,

    /** @brief Info log level */
    SB_INFO,

    /** @brief Warning log level */
    SB_WARN,

    /** @brief Error log level */
    SB_ERROR,
} sb_log_level_t;

/**
 * @typedef sb_log_callback_t
 * @brief Defines the type for logger callback functions in C.
 *
 * This callback function allows clients to provide custom logging logic. It is called
 * for each log message with details such as the message content, log level, source file,
 * and line number.
 *
 * The callback function parameters are:
 * - **message**: The content of the log message as a null-terminated string.
 * - **level**: The severity level of the log message, represented by @ref sb_log_level_t.
 * - **file**: The name of the source file where the log message originated, as a null-terminated
 * string.
 * - **line**: The line number in the source file where the log message originated.
 * - **userData**: A pointer to user-defined data, passed when registering the callback. This
 *                 can be used to maintain context or state within the callback.
 * - **timestamp**: The timestamp of the log message in millseconds since the epoch.
 */
typedef void (*sb_log_callback_t)(const char *message, sb_log_level_t level, const char *file,
                                  int line, int64_t timestamp, void *user_data);

#ifdef __cplusplus
}
#endif
