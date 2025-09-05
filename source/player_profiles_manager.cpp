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
#include "logger.h"

#include <nlohmann/json.hpp>

namespace scorbit {
namespace detail {

bool operator==(const PlayerProfile &lhs, const PlayerProfile &rhs)
{
    return lhs.id == rhs.id && lhs.preferInitials == rhs.preferInitials && lhs.name == rhs.name
        && lhs.initials == rhs.initials && lhs.pictureUrl == rhs.pictureUrl && lhs.url == rhs.url;
}

bool operator!=(const PlayerProfile &lhs, const PlayerProfile &rhs)
{
    return !(lhs == rhs);
}

// -----------------------------------------------------------------------

void PlayerProfilesManager::setProfiles(const nlohmann::json &val)
{
    if (!val.is_array()) {
        WRN("Invalid player profiles data");
        return;
    }

    decltype(m_profiles) profiles;

    for (const auto &obj : val) {
        if (!obj.is_object()) {
            WRN("Invalid player profile data: {}", val.dump());
            continue;
        }

        try {
            sb_player_t playerNum = obj["position"].get<sb_player_t>();

            if (const auto it = obj.find("player"); it != obj.end() && it->is_object()) {
                const auto &player = *it;

                PlayerProfile profile;
                player["id"].get_to(profile.id);
                profile.preferInitials = player.value("prefer_initials", false);
                player["username"].get_to(profile.username);
                player["display_name"].get_to(profile.name);
                player["initials"].get_to(profile.initials);
                player["url"].get_to(profile.url);

                if (const auto it = player.find("avatar"); it != player.end() && it->is_string()) {
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

    // Check if profiles have changed
    {
        std::lock_guard<std::mutex> lock(m_profilesMutex);
        if (profiles != m_profiles) {
            m_profiles = std::move(profiles);
            m_updated = true;
        }
    }
}

void PlayerProfilesManager::setPicture(sb_player_t player, std::vector<uint8_t> &&picture)
{
    std::lock_guard<std::mutex> lock(m_picturesMutex);
    m_picturesCache.put(player, std::move(picture));
    m_updated = true;
}

void PlayerProfilesManager::removePicture(sb_player_t player)
{
    std::lock_guard<std::mutex> lock(m_picturesMutex);
    m_picturesCache.erase(player);
    m_updated = true;
}

/**
 * @brief Once called, it clears update status and prepares data for retrieval
 * @return true if there was any update in profiles
 */
bool PlayerProfilesManager::hasUpdate()
{
    if (m_updated) {
        std::lock_guard<std::mutex> lock(m_profilesMutex);
        m_storedProfiles = m_profiles;
    }
    return m_updated.exchange(false); // return result and clear update flag
}

const PlayerProfile *PlayerProfilesManager::profile(sb_player_t player) const
{
    if (m_storedProfiles.count(player) == 0)
        return nullptr;

    return &m_storedProfiles.at(player);
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
    std::lock_guard<std::mutex> lock(m_profilesMutex);
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
