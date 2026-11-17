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

#pragma once

#include <chrono>
#include <cstdint>

namespace scorbit {
namespace detail {

/**
 * The clock elapsed time is measured on (SB-6367): monotonic, so no UTC correction moves it, and,
 * unlike std::chrono::steady_clock, still counting while the machine is suspended. Time a player
 * spends with the machine asleep is time they took, never time dropped from their result.
 *
 * - Linux: CLOCK_BOOTTIME (steady_clock is CLOCK_MONOTONIC, which stops in suspend).
 * - macOS: CLOCK_MONOTONIC, which counts sleep on Darwin (libc++'s steady_clock is
 *   CLOCK_UPTIME_RAW, which doesn't).
 * - Windows: the interrupt time, which counts sleep and hibernation.
 *
 * Its epoch is the boot, so its instants mean nothing across boots.
 */
struct ElapsedClock {
    using duration = std::chrono::nanoseconds;
    using rep = duration::rep;
    using period = duration::period;
    using time_point = std::chrono::time_point<ElapsedClock>;
    static constexpr bool is_steady = true;

    static time_point now() noexcept;
};

/** The name of the system clock behind @ref ElapsedClock on this machine. */
const char *elapsedClockSource();

/**
 * The finest step, in milliseconds, @ref ElapsedClock can tell apart on this machine: at least 1,
 * as rows carry whole milliseconds, and coarser if the clock itself is. This is the clock's
 * resolution only: a row is stamped when the host commits it, which may lag the event on the
 * machine. That lag is for on-device verification (B05) to measure, and isn't claimed here.
 */
int64_t elapsedClockResolutionMs();

} // namespace detail
} // namespace scorbit
