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

#include "leaderboard_internal.h"

#include <nlohmann/json.hpp>

#include <memory>

using json = nlohmann::json;

namespace {

template<typename T>
bool assignScalar(const json &obj, const char *key, T &out)
{
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return false;
    }

    if constexpr (std::is_same_v<T, bool>) {
        if (!it->is_boolean()) {
            return false;
        }
    } else {
        if (!it->is_number()) {
            return false;
        }
    }

    out = it->get<T>();
    return true;
}

bool assignString(const json &obj, const char *key, std::string &out)
{
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null() || !it->is_string()) {
        return false;
    }

    out = it->get<std::string>();
    return true;
}

const scorbit::detail::LeaderboardEntryData *getEntry(const sb_leaderboard_t *leaderboard,
                                                      size_t index)
{
    if (leaderboard == nullptr || index >= leaderboard->entries.size()) {
        return nullptr;
    }

    return &leaderboard->entries[index];
}

} // namespace

namespace scorbit {
namespace detail {

sb_leaderboard_t *parseLeaderboardJson(const std::string &reply)
{
    auto parsed = json::parse(reply);
    if (!parsed.is_array()) {
        return nullptr;
    }

    auto leaderboard = std::make_unique<sb_leaderboard_t>();
    leaderboard->entries.reserve(parsed.size());

    for (const auto &obj : parsed) {
        if (!obj.is_object()) {
            return nullptr;
        }

        LeaderboardEntryData entry;
        assignScalar(obj, "id", entry.id);
        assignScalar(obj, "rank", entry.rank);
        assignScalar(obj, "high_score", entry.highScore);
        assignString(obj, "image", entry.image);
        assignScalar(obj, "reaction_count", entry.reactionCount);
        assignScalar(obj, "score_count", entry.scoreCount);
        assignScalar(obj, "is_nfc_verified", entry.isNfcVerified);
        assignScalar(obj, "is_verified", entry.isVerified);
        assignScalar(obj, "is_vpin", entry.isVpin);
        assignString(obj, "created", entry.created);

        const auto playerIt = obj.find("player");
        if (playerIt != obj.end() && !playerIt->is_null()) {
            if (!playerIt->is_object()) {
                return nullptr;
            }

            const auto &player = *playerIt;
            assignString(player, "id", entry.player.id);
            assignString(player, "username", entry.player.username);
            assignString(player, "display_name", entry.player.displayName);
            assignString(player, "initials", entry.player.initials);
            assignString(player, "avatar", entry.player.avatar);
            assignScalar(player, "follower_count", entry.player.followerCount);
            assignScalar(player, "following_count", entry.player.followingCount);
            assignString(player, "last_login", entry.player.lastLogin);
        }

        leaderboard->entries.push_back(std::move(entry));
    }

    return leaderboard.release();
}

} // namespace detail
} // namespace scorbit

size_t sb_leaderboard_entries_count(const sb_leaderboard_t *leaderboard)
{
    return leaderboard ? leaderboard->entries.size() : 0;
}

bool sb_leaderboard_entry_id(const sb_leaderboard_t *leaderboard, size_t index, uint64_t *id)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || id == nullptr) {
        return false;
    }

    *id = entry->id;
    return true;
}

bool sb_leaderboard_entry_rank(const sb_leaderboard_t *leaderboard, size_t index, int *rank)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || rank == nullptr) {
        return false;
    }

    *rank = entry->rank;
    return true;
}

bool sb_leaderboard_entry_high_score(const sb_leaderboard_t *leaderboard, size_t index,
                                     sb_score_t *high_score)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || high_score == nullptr) {
        return false;
    }

    *high_score = entry->highScore;
    return true;
}

bool sb_leaderboard_entry_image(const sb_leaderboard_t *leaderboard, size_t index,
                                const char **image)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || image == nullptr || entry->image.empty()) {
        return false;
    }

    *image = entry->image.c_str();
    return true;
}

bool sb_leaderboard_entry_reaction_count(const sb_leaderboard_t *leaderboard, size_t index,
                                         int *reaction_count)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || reaction_count == nullptr) {
        return false;
    }

    *reaction_count = entry->reactionCount;
    return true;
}

bool sb_leaderboard_entry_score_count(const sb_leaderboard_t *leaderboard, size_t index,
                                      int *score_count)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || score_count == nullptr) {
        return false;
    }

    *score_count = entry->scoreCount;
    return true;
}

bool sb_leaderboard_entry_is_nfc_verified(const sb_leaderboard_t *leaderboard, size_t index,
                                          bool *is_nfc_verified)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || is_nfc_verified == nullptr) {
        return false;
    }

    *is_nfc_verified = entry->isNfcVerified;
    return true;
}

bool sb_leaderboard_entry_is_verified(const sb_leaderboard_t *leaderboard, size_t index,
                                      bool *is_verified)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || is_verified == nullptr) {
        return false;
    }

    *is_verified = entry->isVerified;
    return true;
}

bool sb_leaderboard_entry_is_vpin(const sb_leaderboard_t *leaderboard, size_t index, bool *is_vpin)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || is_vpin == nullptr) {
        return false;
    }

    *is_vpin = entry->isVpin;
    return true;
}

bool sb_leaderboard_entry_created(const sb_leaderboard_t *leaderboard, size_t index,
                                  const char **created)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || created == nullptr || entry->created.empty()) {
        return false;
    }

    *created = entry->created.c_str();
    return true;
}

bool sb_leaderboard_entry_player_id(const sb_leaderboard_t *leaderboard, size_t index,
                                    const char **id)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || id == nullptr || entry->player.id.empty()) {
        return false;
    }

    *id = entry->player.id.c_str();
    return true;
}

bool sb_leaderboard_entry_player_username(const sb_leaderboard_t *leaderboard, size_t index,
                                          const char **username)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || username == nullptr || entry->player.username.empty()) {
        return false;
    }

    *username = entry->player.username.c_str();
    return true;
}

bool sb_leaderboard_entry_player_display_name(const sb_leaderboard_t *leaderboard, size_t index,
                                              const char **display_name)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || display_name == nullptr || entry->player.displayName.empty()) {
        return false;
    }

    *display_name = entry->player.displayName.c_str();
    return true;
}

bool sb_leaderboard_entry_player_initials(const sb_leaderboard_t *leaderboard, size_t index,
                                          const char **initials)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || initials == nullptr || entry->player.initials.empty()) {
        return false;
    }

    *initials = entry->player.initials.c_str();
    return true;
}

bool sb_leaderboard_entry_player_avatar(const sb_leaderboard_t *leaderboard, size_t index,
                                        const char **avatar)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || avatar == nullptr || entry->player.avatar.empty()) {
        return false;
    }

    *avatar = entry->player.avatar.c_str();
    return true;
}

bool sb_leaderboard_entry_player_follower_count(const sb_leaderboard_t *leaderboard, size_t index,
                                                int *follower_count)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || follower_count == nullptr) {
        return false;
    }

    *follower_count = entry->player.followerCount;
    return true;
}

bool sb_leaderboard_entry_player_following_count(const sb_leaderboard_t *leaderboard, size_t index,
                                                 int *following_count)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || following_count == nullptr) {
        return false;
    }

    *following_count = entry->player.followingCount;
    return true;
}

bool sb_leaderboard_entry_player_last_login(const sb_leaderboard_t *leaderboard, size_t index,
                                            const char **last_login)
{
    const auto *entry = getEntry(leaderboard, index);
    if (entry == nullptr || last_login == nullptr || entry->player.lastLogin.empty()) {
        return false;
    }

    *last_login = entry->player.lastLogin.c_str();
    return true;
}
