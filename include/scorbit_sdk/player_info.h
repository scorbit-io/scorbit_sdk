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
#include <string>
#include <vector>

namespace scorbit {

using Picture = std::vector<uint8_t>; // The profile picture binary (jpg)

struct PlayerInfo {
    bool hasInfo() const { return !id.empty(); }

    std::string id;            /// The player's ID
    std::string preferredName; /// Either name or initials
    std::string name;          /// The player's name to display
    std::string initials;      /// The player's initials, e.g. "DTM"
    std::string pictureUrl;    /// The URL of the profile picture
    std::string claimDeeplink; /// Claim URL for unclaimed player slots (empty if claimed)
};

/// One position of a @ref SessionPlayers change.
struct SessionPlayer {
    unsigned position {0};   /// The position, from 1
    bool claimed {false};    /// True when a player has claimed the position
    std::string playerId;    /// The player's id, empty when unclaimed
    std::string displayName; /// The player's display name (as in the lobby), empty when none
};

/// The running session's players: see @ref scorbit::EventType::SessionPlayersChanged.
struct SessionPlayers {
    std::string venueMachine;           /// The machine whose game it is; ignore it if not current
    std::string sessionUuid;            /// The session the players are for
    bool isFirst {false};               /// The session's first event: its baseline
    bool gameActive {false};            /// True while the game is in progress
    std::vector<SessionPlayer> players; /// Every position, in order
};
}
