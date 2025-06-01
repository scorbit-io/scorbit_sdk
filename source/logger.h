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

#include <scorbit_sdk/log_types.h>
#include <scorbit_sdk/export.h>

#include <fmt/format.h>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace scorbit {
namespace detail {

class Logger
{
    friend Logger *logger();

    struct LogData {
        std::string message; // Log message
        LogLevel level;      // Log level
        const char *file;    // Source file name
        int line;            // Source line number
        int64_t timestamp;   // Timestamp in milliseconds since epoch
    };

    struct CallbackAndData {
        LoggerCallback callback;
        void *userData {nullptr};
    };

public:
    static Logger *instance();

    ~Logger();

    void addCallback(LoggerCallback &&callback, void *userData = nullptr);
    void clear();

    void log(const std::string &message, LogLevel level, const char *file, int line);

private:
    Logger();
    void processLogs();

private:
    std::atomic_bool m_stop {false};
    std::queue<LogData> m_queue;
    std::vector<CallbackAndData> m_callbacks;

    std::mutex m_cbMutex;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCV;

    std::thread m_thread; // This should be last, other members must be valid when it is destroyed
};

extern Logger *logger();

// Macros to make the logMessage function easier to call with added filename and line number
#define DBG(...) scorbit::detail::logMessage(LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define INF(...) scorbit::detail::logMessage(LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define WRN(...) scorbit::detail::logMessage(LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define ERR(...) scorbit::detail::logMessage(LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)

template<typename... Args>
constexpr inline void logMessage(LogLevel level, const char *file, int line, Args &&...args)
{
    // Instead of full file path, we just want the file name
    Logger::instance()->log(fmt::format(std::forward<Args>(args)...), level,
                            file + std::string_view(file).find_last_of("/\\") + 1, line);
}

} // namespace detail
} // namespace scorbit
