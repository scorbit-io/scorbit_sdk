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

#include <scorbit_sdk/leaderboard_c.h>
#include <scorbit_sdk/net_types.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace scorbit {

struct LeaderboardPlayer {
    std::string id;
    std::string username;
    std::string displayName;
    std::string initials;
    std::string avatarUrl;
    int followerCount {0};
    int followingCount {0};
    std::string lastLogin;
};

struct LeaderboardEntry {
    uint64_t id {0};
    int rank {0};
    LeaderboardPlayer player;
    sb_score_t highScore {0};
    std::string image;
    int reactionCount {0};
    int scoreCount {0};
    bool isNfcVerified {false};
    bool isVerified {false};
    bool isVpin {false};
    std::string created;
};

struct LeaderboardResult {
    std::vector<LeaderboardEntry> entries;

    bool empty() const { return entries.empty(); }
    size_t size() const { return entries.size(); }

    static LeaderboardResult fromC(const sb_leaderboard_t *leaderboard)
    {
        LeaderboardResult result;
        const auto count = ::sb_leaderboard_entries_count(leaderboard);
        result.entries.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            LeaderboardEntry entry;
            const char *str = nullptr;

            ::sb_leaderboard_entry_id(leaderboard, i, &entry.id);
            ::sb_leaderboard_entry_rank(leaderboard, i, &entry.rank);
            ::sb_leaderboard_entry_high_score(leaderboard, i, &entry.highScore);
            if (::sb_leaderboard_entry_image(leaderboard, i, &str) && str) {
                entry.image = str;
            }
            ::sb_leaderboard_entry_reaction_count(leaderboard, i, &entry.reactionCount);
            ::sb_leaderboard_entry_score_count(leaderboard, i, &entry.scoreCount);
            ::sb_leaderboard_entry_is_nfc_verified(leaderboard, i, &entry.isNfcVerified);
            ::sb_leaderboard_entry_is_verified(leaderboard, i, &entry.isVerified);
            ::sb_leaderboard_entry_is_vpin(leaderboard, i, &entry.isVpin);
            if (::sb_leaderboard_entry_created(leaderboard, i, &str) && str) {
                entry.created = str;
            }

            if (::sb_leaderboard_entry_player_id(leaderboard, i, &str) && str) {
                entry.player.id = str;
            }
            if (::sb_leaderboard_entry_player_username(leaderboard, i, &str) && str) {
                entry.player.username = str;
            }
            if (::sb_leaderboard_entry_player_display_name(leaderboard, i, &str) && str) {
                entry.player.displayName = str;
            }
            if (::sb_leaderboard_entry_player_initials(leaderboard, i, &str) && str) {
                entry.player.initials = str;
            }
            if (::sb_leaderboard_entry_player_avatar(leaderboard, i, &str) && str) {
                entry.player.avatarUrl = str;
            }
            ::sb_leaderboard_entry_player_follower_count(leaderboard, i,
                                                         &entry.player.followerCount);
            ::sb_leaderboard_entry_player_following_count(leaderboard, i,
                                                          &entry.player.followingCount);
            if (::sb_leaderboard_entry_player_last_login(leaderboard, i, &str) && str) {
                entry.player.lastLogin = str;
            }

            result.entries.push_back(std::move(entry));
        }

        return result;
    }
};

using LeaderboardCallback = std::function<void(Error error, const LeaderboardResult &leaderboard)>;

} // namespace scorbit
