/*
 * Logger Interface Library
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

#include <fmt/format.h>
#include <string>
#include <string_view>

namespace logger {

/**
 * @enum LogLevel
 * @brief Defines the log levels for the logger.
 */
enum class LogLevel : int {
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
 * @brief Log a message with the given level, file, and line.
 *
 * This function must be implemented by a logger backend (logger_callback or logger_spdlog).
 * The implementation is linked at build time.
 *
 * @param message The log message.
 * @param level The log level.
 * @param file The source file name (basename only).
 * @param line The line number.
 */
void log(const std::string &message, LogLevel level, const char *file, int line);

/**
 * @brief Template function to format and log a message.
 *
 * @tparam Args Variadic template arguments for fmt::format.
 * @param level The log level.
 * @param file The source file path.
 * @param line The line number.
 * @param args Format string and arguments.
 */
template<typename... Args>
constexpr inline void logMessage(LogLevel level, const char *file, int line, Args &&...args)
{
    // Extract just the filename from the full path
    std::string_view filePath(file);
    auto pos = filePath.find_last_of("/\\");
    const char *fileName = (pos != std::string_view::npos) ? file + pos + 1 : file;

    log(fmt::format(std::forward<Args>(args)...), level, fileName, line);
}

} // namespace logger

// Logging macros for convenient usage
#define DBG(...) logger::logMessage(logger::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define INF(...) logger::logMessage(logger::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define WRN(...) logger::logMessage(logger::LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define ERR(...) logger::logMessage(logger::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
