/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * May 2025
 *
 ****************************************************************************/

#include "player_profiles_manager.h"
#include "logger.h"

namespace scorbit {
namespace detail {

bool operator==(const PlayerProfile &lhs, const PlayerProfile &rhs)
{
    return lhs.id == rhs.id && lhs.preferInitials == rhs.preferInitials && lhs.name == rhs.name
        && lhs.initials == rhs.initials && lhs.pictureUrl == rhs.pictureUrl;
}

bool operator!=(const PlayerProfile &lhs, const PlayerProfile &rhs)
{
    return !(lhs == rhs);
}

// -----------------------------------------------------------------------

void PlayerProfilesManager::setProfiles(const boost::json::value &val)
{
    if (!val.is_array()) {
        WRN("Invalid player profiles data");
        return;
    }

    decltype(m_profiles) profiles;

    for (const auto &item : val.as_array()) {
        if (!item.is_object()) {
            WRN("Invalid player profile data: {}", boost::json::serialize(val));
            continue;
        }

        try {
            sb_player_t playerNum = item.at("position").as_int64();

            if (item.at("player").is_object()) {
                const auto &player = item.at("player").get_object();

                PlayerProfile profile;
                profile.id = player.at("id").to_number<uint64_t>();
                profile.preferInitials = player.at("prefer_initials").as_bool();
                profile.name = player.at("cached_display_name").as_string();
                profile.initials = player.at("initials").as_string();
                profile.pictureUrl = player.at("profile_picture").as_string();

                profiles.emplace(playerNum, std::move(profile));
            }
        } catch (const std::exception &e) {
            ERR("Failed to parse player profile: {}, item: {}", e.what(),
                boost::json::serialize(item));
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
