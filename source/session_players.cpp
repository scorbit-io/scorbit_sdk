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

#include "session_players.h"
#include "identifiers.h"

namespace scorbit {
namespace detail {

namespace {

// Positions beyond this are not a real game; a bogus number must not allocate a huge vector
constexpr unsigned MAX_POSITION {16};

} // namespace

std::vector<SessionPlayerState> sessionPlayersFromScores(const nlohmann::json &scores)
{
    std::vector<SessionPlayerState> players;
    if (!scores.is_array()) {
        return players;
    }
    for (const auto &obj : scores) {
        if (!obj.is_object()) {
            continue;
        }
        const auto positionIt = obj.find(JKEY_SCR_POSITION);
        if (positionIt == obj.end() || !positionIt->is_number_unsigned()) {
            continue;
        }
        const auto position = positionIt->get<unsigned>();
        if (position == 0 || position > MAX_POSITION) {
            continue;
        }
        if (position > players.size()) {
            players.resize(position);
        }
        auto &state = players[position - 1];
        state = {};
        // An unclaimed position has a null player
        if (const auto player = obj.find(JKEY_SCR_PLAYER); player != obj.end() && player->is_object()) {
            state.claimed = true;
            if (const auto id = player->find(JKEY_PLAYER_ID); id != player->end() && id->is_string()) {
                state.playerId = id->get<std::string>();
            }
            if (const auto name = player->find(JKEY_PLAYER_DISPLAY_NAME);
                name != player->end() && name->is_string()) {
                state.displayName = name->get<std::string>();
            }
        }
    }
    return players;
}

std::optional<SessionPlayersTracker::Change>
SessionPlayersTracker::update(const std::string &sessionUuid, std::vector<SessionPlayerState> players)
{
    if (sessionUuid.empty()) {
        return std::nullopt;
    }
    std::scoped_lock lock(m_mutex);
    for (auto &[uuid, last] : m_sessions) {
        if (uuid != sessionUuid) {
            continue;
        }
        if (last == players) {
            return std::nullopt;
        }
        last = players;
        return Change {false, std::move(players)};
    }
    m_sessions.emplace_back(sessionUuid, players);
    while (m_sessions.size() > MAX_SESSIONS) {
        m_sessions.pop_front();
    }
    return Change {true, std::move(players)};
}

void SessionPlayersTracker::clear()
{
    std::scoped_lock lock(m_mutex);
    m_sessions.clear();
}

} // namespace detail
} // namespace scorbit
