/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * May 2025
 *
 ****************************************************************************/

#pragma once

#include <scorbit_sdk/common_types_c.h>
#include "utils/lru_cache.hpp"

#include <boost/json.hpp>

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <mutex>

namespace scorbit {
namespace detail {

constexpr auto MAX_PICTURES_CACHED = 8; // Maximum number of pictures to cache

/**
 * @brief PlayerProfile holds the player profile information.
 * It is used to display player information in the UI.
 */
struct PlayerProfile {
    uint64_t id;
    bool preferInitials;    // Reference to either display_name or initials
    std::string name;       // The player's name to display
    std::string initials;   // The player's initials, e.g. "DTM"
    std::string pictureUrl; // The URL to the player's profile picture
};

using Picture = std::vector<uint8_t>; // The profile picture binary (jpg)

bool operator==(const PlayerProfile &lhs, const PlayerProfile &rhs);
bool operator!=(const PlayerProfile &lhs, const PlayerProfile &rhs);

/**
 * @brief PlayerProfilesManager manages the player profiles.
 * It is used to set and get player profiles.
 *
 * Requirements:
 * 1. Hold all profiles. Set by json data. It can be changed at any time from another thread.
 * 2. Return pointer to profile by player number. If profile is not found, return nullptr.
 *    Returned pointer is valid until the next call to get profile.
 */
class PlayerProfilesManager
{
public:
    void setProfiles(const boost::json::value &val);
    void setPicture(sb_player_t player, std::vector<uint8_t> &&picture);
    void removePicture(sb_player_t player);

    bool hasUpdate();
    const PlayerProfile *profile(sb_player_t player) const;

    bool hasPicture(sb_player_t player) const;
    const Picture &picture(sb_player_t player) const;

    std::map<sb_player_t, std::string> picturesToDownload() const;

private:
    std::atomic_bool m_updated {false}; // Flag to indicate if any profile has been changed
    std::map<sb_player_t, PlayerProfile> m_profiles;
    std::map<sb_player_t, PlayerProfile> m_storedProfiles;
    mutable Picture m_storedPicture;
    mutable LRUCache<sb_player_t, Picture> m_picturesCache {MAX_PICTURES_CACHED};
    mutable std::mutex m_profilesMutex; // Mutex to protect access to profiles
    mutable std::mutex m_picturesMutex; // Mutex to protect access to cached pictures
};

} // namespace detail
} // namespace scorbit
