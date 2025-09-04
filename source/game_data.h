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

#include <scorbit_sdk/common_types_c.h>
#include "player_state.h"
#include "modes.h"
#include <map>
#include <chrono>

namespace scorbit {
namespace detail {

struct GameData {
    int id {0};
    bool isGameActive {false};

    sb_ball_t ball {0};
    sb_player_t activePlayer {0};
    std::map<sb_player_t, PlayerState> players;
    Modes modes;

    std::chrono::time_point<std::chrono::system_clock> timestamp;
};

inline bool operator==(const scorbit::detail::GameData &lhs, const scorbit::detail::GameData &rhs)
{
    return lhs.isGameActive == rhs.isGameActive && lhs.ball == rhs.ball
        && lhs.activePlayer == rhs.activePlayer && lhs.modes == rhs.modes
        && lhs.players == rhs.players;
}

inline bool operator!=(const scorbit::detail::GameData &lhs, const scorbit::detail::GameData &rhs)
{
    return !(lhs == rhs);
}

using GameHistory = std::vector<GameData>;

} // namespace detail
} // namespace scorbit
