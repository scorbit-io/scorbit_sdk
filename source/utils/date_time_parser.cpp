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

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/time_facet.hpp>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <time.h>
#endif

namespace scorbit {
namespace detail {

// Function to parse HTTP date header and convert it to Unix timestamp
int64_t parseHttpDateToUnixTimestamp(const std::string &httpDate)
{
    using namespace boost::posix_time;

    // Create a time input facet for the HTTP date format
    time_input_facet *facet = new time_input_facet("%a, %d %b %Y %H:%M:%S GMT");
    std::istringstream ss(httpDate);
    ss.imbue(std::locale {ss.getloc(), facet});

    ptime pt;
    ss >> pt;

    if (pt == ptime()) {
        return -1;
    }

    // Convert ptime to Unix timestamp
    ptime epoch(boost::gregorian::date(1970, 1, 1));
    time_duration diff = pt - epoch;
    return diff.total_seconds();
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
