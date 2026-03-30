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

#include "player_profiles_manager.h"
#include <logger/logger.h>
#include "identifiers.h"

#include <nlohmann/json.hpp>

namespace scorbit {
namespace detail {

bool operator==(const PlayerProfile &lhs, const PlayerProfile &rhs)
{
    return lhs.player == rhs.player && lhs.id == rhs.id && lhs.preferInitials == rhs.preferInitials
        && lhs.name == rhs.name && lhs.initials == rhs.initials && lhs.pictureUrl == rhs.pictureUrl
        && lhs.url == rhs.url;
}

bool operator!=(const PlayerProfile &lhs, const PlayerProfile &rhs)
{
    return !(lhs == rhs);
}

// -----------------------------------------------------------------------

std::optional<std::vector<PlayerProfile>>
PlayerProfilesManager::setProfiles(const nlohmann::json &val)
{
    if (!val.is_array()) {
        WRN("Invalid player profiles data");
        return std::nullopt;
    }

    decltype(m_profiles) profiles;

    for (const auto &obj : val) {
        if (!obj.is_object()) {
            WRN("Invalid player profile data: {}", val.dump());
            continue;
        }

        try {
            sb_player_t playerNum = obj[JKEY_SCR_POSITION].get<sb_player_t>();

            if (const auto it = obj.find(JKEY_SCR_PLAYER); it != obj.end() && it->is_object()) {
                const auto &player = *it;

                PlayerProfile profile;
                profile.player = playerNum;
                player[JKEY_PLAYER_ID].get_to(profile.id);
                profile.preferInitials = player.value(JKEY_PLAYER_PREFER_INITIALS, false);
                player[JKEY_PLAYER_USERNAME].get_to(profile.username);
                player[JKEY_PLAYER_DISPLAY_NAME].get_to(profile.name);
                player[JKEY_PLAYER_INITIALS].get_to(profile.initials);
                player[JKEY_PLAYER_URL].get_to(profile.url);

                if (const auto it = player.find(JKEY_PLAYER_AVATAR);
                    it != player.end() && it->is_string()) {
                    it->get_to(profile.pictureUrl);
                } else {
                    profile.pictureUrl.clear();
                }

                profiles.emplace(playerNum, std::move(profile));
            }
        } catch (const std::exception &e) {
            ERR("Failed to parse player profile: {}, item: {}", e.what(), obj.dump());
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_profilesMutex);
        if (profiles != m_profiles) {
            m_profiles = std::move(profiles);

            std::vector<PlayerProfile> profilesVector;
            for (const auto &profile : m_profiles) {
                profilesVector.push_back(profile.second);
            }
            return profilesVector;
        }
    }
    return std::nullopt;
}

void PlayerProfilesManager::setPicture(sb_player_t player, std::vector<uint8_t> picture)
{
    std::lock_guard<std::mutex> lock(m_picturesMutex);
    m_picturesCache.put(player, std::move(picture));
}

void PlayerProfilesManager::removePicture(sb_player_t player)
{
    std::lock_guard<std::mutex> lock(m_picturesMutex);
    m_picturesCache.erase(player);
}

std::optional<PlayerProfile> PlayerProfilesManager::profile(sb_player_t player) const
{
    std::lock_guard<std::mutex> lock(m_profilesMutex);
    if (m_profiles.count(player) == 0) {
        return std::nullopt;
    }

    return m_profiles.at(player);
}

bool PlayerProfilesManager::hasPicture(sb_player_t player) const
{
    std::lock_guard<std::mutex> lock(m_picturesMutex);
    return m_picturesCache.has(player);
}

const Picture &PlayerProfilesManager::picture(sb_player_t player) const
{
    std::lock_guard<std::mutex> lock(m_picturesMutex);
    m_storedPicture.clear();
    m_picturesCache.get(player, m_storedPicture);
    return m_storedPicture;
}

std::map<sb_player_t, std::string> PlayerProfilesManager::picturesToDownload() const
{
    std::map<sb_player_t, std::string> picturesToDownload;

    std::scoped_lock lock(m_profilesMutex, m_picturesMutex);

    for (const auto &[playerNum, profile] : m_profiles) {
        if (profile.pictureUrl.empty())
            continue;

        if (!m_picturesCache.has(playerNum)) {
            picturesToDownload.emplace(playerNum, profile.pictureUrl);
        }
    }
    return picturesToDownload;
}

} // namespace detail
} // namespace scorbit
