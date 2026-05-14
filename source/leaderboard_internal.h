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

#include <functional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {

struct LeaderboardPlayerData {
    std::string id;
    std::string username;
    std::string displayName;
    std::string initials;
    std::string avatar;
    int followerCount {0};
    int followingCount {0};
    std::string lastLogin;
};

struct LeaderboardEntryData {
    uint64_t id {0};
    int rank {0};
    LeaderboardPlayerData player;
    sb_score_t highScore {0};
    std::string image;
    int reactionCount {0};
    int scoreCount {0};
    bool isNfcVerified {false};
    bool isVerified {false};
    bool isVpin {false};
    std::string created;
};

using LeaderboardHandleCallback = std::function<void(Error error, sb_leaderboard_t *leaderboard)>;

sb_leaderboard_t *parseLeaderboardJson(const std::string &reply);

} // namespace detail
} // namespace scorbit

struct sb_leaderboard_t {
    std::vector<scorbit::detail::LeaderboardEntryData> entries;
};

namespace scorbit {
namespace detail {

/** Non-exported: frees heap leaderboards from parseLeaderboardJson (not request_top_scores handles). */
inline void destroyLeaderboard(sb_leaderboard_t *leaderboard)
{
    delete leaderboard;
}

} // namespace detail
} // namespace scorbit
