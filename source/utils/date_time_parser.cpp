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

#include "date_time_parser.h"

#include <chrono>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <time.h>
#endif

namespace scorbit {
namespace detail {

int64_t parseHttpDateToUnixTimestamp(const std::string &httpDate)
{
    std::tm tm {};
    std::istringstream ss(httpDate);
    ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");

    if (ss.fail()) {
        return -1;
    }

    // timegm interprets tm as UTC (unlike mktime which uses local time)
#ifdef _WIN32
    const auto epoch = _mkgmtime(&tm);
#else
    const auto epoch = timegm(&tm);
#endif

    if (epoch == static_cast<time_t>(-1)) {
        return -1;
    }

    return static_cast<int64_t>(epoch);
}

bool setSystemTime(int64_t timestamp)
{
#ifdef _WIN32
    // Windows FILETIME: 100-ns ticks since 1601-01-01 UTC
    constexpr int64_t EPOCH_DIFF_SECONDS = 11644473600LL;
    constexpr int64_t TICKS_PER_SECOND = 10'000'000LL;

    uint64_t ft_ticks = (static_cast<uint64_t>(timestamp) + EPOCH_DIFF_SECONDS) * TICKS_PER_SECOND;

    FILETIME ft;
    ft.dwLowDateTime = static_cast<DWORD>(ft_ticks);
    ft.dwHighDateTime = static_cast<DWORD>(ft_ticks >> 32);

    // Windows 10+
    if (auto fn = reinterpret_cast<decltype(&SetSystemTimePreciseAsFileTime)>(GetProcAddress(
                GetModuleHandleA("kernel32.dll"), "SetSystemTimePreciseAsFileTime"))) {
        return fn(&ft) != 0;
    }

    SYSTEMTIME st {};
    if (!FileTimeToSystemTime(&ft, &st))
        return false;

    return SetSystemTime(&st) != 0;

#else
    // Linux / POSIX
    timespec ts {};
    ts.tv_sec = timestamp;
    ts.tv_nsec = 0;

    return clock_settime(CLOCK_REALTIME, &ts) == 0;
#endif
}

} // namespace detail
} // namespace scorbit
