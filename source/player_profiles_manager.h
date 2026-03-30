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
#include "utils/lru_cache.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <optional>

namespace scorbit {
namespace detail {

constexpr auto MAX_PICTURES_CACHED = 8; // Maximum number of pictures to cache

/**
 * @brief PlayerProfile holds the player profile information.
 * It is used to display player information in the UI.
 */
struct PlayerProfile {
    sb_player_t player {0};
    bool preferInitials {false};
    std::string id;
    std::string username;
    std::string name;
    std::string initials;
    std::string pictureUrl;
    std::string url;
    std::string claimDeeplink;

    bool hasInfo() const { return !id.empty(); }
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
    /// Returns the new profiles vector if data changed, std::nullopt if unchanged.
    std::optional<std::vector<PlayerProfile>> setProfiles(const nlohmann::json &val,
                                                          const std::string &machineUuid);

    void setPicture(const std::string &avatarUrl, std::vector<uint8_t> picture);
    void removePicture(const std::string &avatarUrl);

    std::optional<PlayerProfile> profile(sb_player_t player) const;

    bool hasPicture(const std::string &avatarUrl) const;
    const Picture &picture(const std::string &avatarUrl) const;

    std::map<sb_player_t, std::string> picturesToDownload() const;

private:
    std::vector<PlayerProfile> m_profiles;
    mutable Picture m_storedPicture;
    mutable LRUCache<std::string, Picture> m_picturesCache {MAX_PICTURES_CACHED};
    mutable std::mutex m_profilesMutex;
    mutable std::mutex m_picturesMutex;
};

} // namespace detail
} // namespace scorbit
