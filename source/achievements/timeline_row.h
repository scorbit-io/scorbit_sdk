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
#include <string>
#include <utility>
#include <vector>

namespace scorbit {
namespace detail {
namespace achievements {

using PlayerNumber = unsigned int;
using BallNumber = unsigned int;

/** One committed row of the session timeline (§4.1), as seen by the fact model. */
struct TimelineRow {
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
};

} // namespace achievements
} // namespace detail
} // namespace scorbit
