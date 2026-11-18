/*
 * Scorbit SDK
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

#include "elapsed_clock.h"
#include <algorithm>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <time.h>
#endif

namespace scorbit {
namespace detail {

namespace {

constexpr int64_t NS_PER_MS = 1'000'000;

/** @p ns rounded up to whole milliseconds, and never under 1. */
int64_t atLeastOneMs(int64_t ns)
{
    return std::max<int64_t>(1, (ns + NS_PER_MS - 1) / NS_PER_MS);
}

#ifdef _WIN32

// Interrupt time is in 100 ns units
constexpr int64_t NS_PER_INTERRUPT_TICK = 100;

using InterruptTimeQuery = void(WINAPI *)(PULONGLONG);

/**
 * QueryInterruptTimePrecise (Windows 10 and later), resolved at run time so the SDK still loads
 * on older Windows; null there, where GetTickCount64, also counting sleep, stands in.
 */
InterruptTimeQuery interruptTimeQuery()
{
    static const auto query = []() -> InterruptTimeQuery {
        const auto module = GetModuleHandleW(L"kernelbase.dll");
        if (!module) {
            return nullptr;
        }
        // Through void (*)(), the one function pointer type any other converts to unwarned
        const auto proc = GetProcAddress(module, "QueryInterruptTimePrecise");
        return reinterpret_cast<InterruptTimeQuery>(reinterpret_cast<void (*)()>(proc));
    }();
    return query;
}

#else

#    ifdef __APPLE__
constexpr clockid_t ELAPSED_CLOCK_ID = CLOCK_MONOTONIC;
constexpr const char *ELAPSED_CLOCK_NAME = "CLOCK_MONOTONIC";
#    else
// The Linux UAPI value; the armhf toolchain's older glibc headers lack it.
#        ifndef CLOCK_BOOTTIME
#            define CLOCK_BOOTTIME 7
#        endif
constexpr clockid_t ELAPSED_CLOCK_ID = CLOCK_BOOTTIME;
constexpr const char *ELAPSED_CLOCK_NAME = "CLOCK_BOOTTIME";
#    endif

int64_t toNs(const timespec &ts)
{
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

#endif

int64_t readElapsedClockResolutionMs()
{
#ifdef _WIN32
    if (interruptTimeQuery()) {
        return atLeastOneMs(NS_PER_INTERRUPT_TICK);
    }
    // GetTickCount64 moves once per system timer tick, given in 100 ns units
    DWORD adjustment = 0;
    DWORD increment = 0;
    BOOL disabled = FALSE;
    if (!GetSystemTimeAdjustment(&adjustment, &increment, &disabled) || increment == 0) {
        return atLeastOneMs(16 * NS_PER_MS); // the default tick, 15.625 ms, rounded up
    }
    return atLeastOneMs(static_cast<int64_t>(increment) * NS_PER_INTERRUPT_TICK);
#else
    timespec res {};
    if (clock_getres(ELAPSED_CLOCK_ID, &res) != 0) {
        return 1;
    }
    return atLeastOneMs(toNs(res));
#endif
}

} // namespace

ElapsedClock::time_point ElapsedClock::now() noexcept
{
#ifdef _WIN32
    if (const auto query = interruptTimeQuery()) {
        ULONGLONG ticks = 0;
        query(&ticks);
        return time_point {duration {static_cast<int64_t>(ticks) * NS_PER_INTERRUPT_TICK}};
    }
    return time_point {std::chrono::milliseconds {static_cast<int64_t>(GetTickCount64())}};
#else
    timespec ts {};
    clock_gettime(ELAPSED_CLOCK_ID, &ts);
    return time_point {duration {toNs(ts)}};
#endif
}

const char *elapsedClockSource()
{
#ifdef _WIN32
    return interruptTimeQuery() ? "QueryInterruptTimePrecise" : "GetTickCount64";
#else
    return ELAPSED_CLOCK_NAME;
#endif
}

int64_t elapsedClockResolutionMs()
{
    // Fixed for the process: read once, not for every result that carries it
    static const int64_t resolutionMs = readElapsedClockResolutionMs();
    return resolutionMs;
}

} // namespace detail
} // namespace scorbit
