/*
 * Logger Callback Implementation
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include <logger/logger.h>
#include <cstdint>
#include <functional>
#include <string>

namespace logger {

/**
 * @typedef LoggerCallback
 * @brief Defines the type for logger callback functions.
 *
 * A callback function allows clients to provide custom logging logic.
 *
 * @param message The log message content.
 * @param level The log level.
 * @param file The source file name where the log originated.
 * @param line The line number in the source file.
 * @param timestamp The timestamp of the log message in milliseconds since the epoch.
 */
using LoggerCallback = std::function<void(const std::string &message, LogLevel level,
                                          const char *file, int line, int64_t timestamp)>;

/**
 * @brief Add a logger callback function to be invoked for log messages.
 *
 * This function registers a callback that will be called for each log message.
 * Multiple callbacks can be registered. Log messages are processed asynchronously
 * on a dedicated background thread, ensuring thread safety.
 *
 * @param callback The callback function to register.
 * @param maxLength Maximum length of the log message passed to the callback.
 *                  Messages longer than this will be truncated with an ellipsis.
 */
void addCallback(LoggerCallback &&callback, size_t maxLength = 512);

/**
 * @brief Clears all previously added logger callbacks.
 *
 * After this call, no logger callback will be invoked until a new one is added.
 */
void resetCallbacks();

} // namespace logger
