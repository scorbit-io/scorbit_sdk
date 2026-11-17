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

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

using PlayerNumber = unsigned int;
using BallNumber = unsigned int;

/**
 * The body of @p mode, what follows its first `:`, e.g. `NA{blue}:Kraken` -> `Kraken`: modes are
 * matched by body alone, so a game's category or display color doesn't break its achievements.
 * A name without a `:` is all body.
 */
inline std::string modeBody(std::string_view mode)
{
    const auto colon = mode.find(':');
    return std::string {colon == std::string_view::npos ? mode : mode.substr(colon + 1)};
}

/** One committed row of the session timeline (§4.1), as seen by the fact model. */
struct TimelineRow {
    /** Device UTC: wall clock, which NTP may step. For schedules and calendars, not durations. */
    int64_t timeMs {0};
    PlayerNumber player {0};
    BallNumber ball {0};
    std::map<PlayerNumber, int64_t> scores;

    /** State: modes active at this instant. */
    std::vector<std::string> modes;

    /** Edge: modes completed at this instant only. */
    std::vector<std::string> completedModes;

    /** Edge: event operations (`name += value`) applied at this instant only, in order. */
    std::vector<std::pair<std::string, int64_t>> events;

    /**
     * Whether @ref timeMs came from a clock the server vouched for. Until then local time can't
     * be trusted, so nothing in this row's period can be placed in a `CALENDAR` window (§5.5.2).
     */
    bool clockSynchronised {true};

    // Last, so rows built positionally before these existed keep their meaning

    /**
     * When the row was committed on this boot's monotonic clock, which no UTC correction moves: the
     * clock competition durations are measured on. Meaningless across boots, so never stored.
     * nullopt if the row's source didn't record it, in which case nothing can be timed on it.
     */
    std::optional<int64_t> elapsedMs;

    /**
     * The row's place in the order of rows committed by this game state, counted from 1 for its
     * whole life and running on across games, so a game's start row is not numbered 1; 0 if
     * unknown. Rows of one game are consecutive, so a gap between two of them shows a row was lost.
     */
    uint64_t sequence {0};
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
