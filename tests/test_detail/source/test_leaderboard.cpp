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

#include <scorbit_sdk/leaderboard.h>
#include "leaderboard_internal.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace scorbit;
using namespace scorbit::detail;

namespace {

auto sampleLeaderboardJson()
{
    return R"([
        {
            "id": 3828382,
            "rank": 1,
            "player": {
                "id": "016ae0a4-b1f8-7fc5-ba90-bd106d680829",
                "username": "dilshodm",
                "display_name": "Dilshod",
                "initials": "DTM",
                "avatar": "https://cdn-staging.scorbit.io/profile_pictures/dilshodm.jpg",
                "follower_count": 14,
                "following_count": 10,
                "last_login": "2026-05-13T14:57:07.616568Z",
                "html_url": "https://staging.scorbit.io/@dilshodm/",
                "url": "https://staging.scorbit.io/api/v2/users/dilshodm/"
            },
            "high_score": 146250,
            "image": "https://cdn-staging.scorbit.io/leaderboards/3828382.jpg",
            "reaction_count": 0,
            "score_count": 12,
            "is_nfc_verified": false,
            "is_verified": true,
            "is_vpin": false,
            "html_url": "https://staging.scorbit.io/score/3828382/",
            "url": "https://staging.scorbit.io/api/v2/scores/3828382/",
            "created": "2026-03-31T08:14:10.091057Z"
        }
    ])";
}

using LeaderboardPtr = std::unique_ptr<sb_leaderboard_t, decltype(&detail::destroyLeaderboard)>;

} // namespace

TEST_CASE("Leaderboard JSON parses into helper-backed result")
{
    LeaderboardPtr leaderboard(parseLeaderboardJson(sampleLeaderboardJson()),
                               &detail::destroyLeaderboard);
    REQUIRE(leaderboard);
    REQUIRE(sb_leaderboard_entries_count(leaderboard.get()) == 1);

    uint64_t id = 0;
    int rank = 0;
    sb_score_t highScore = 0;
    int scoreCount = 0;
    bool isVerified = false;
    const char *image = nullptr;
    const char *displayName = nullptr;
    const char *avatar = nullptr;
    const char *created = nullptr;

    REQUIRE(sb_leaderboard_entry_id(leaderboard.get(), 0, &id));
    REQUIRE(sb_leaderboard_entry_rank(leaderboard.get(), 0, &rank));
    REQUIRE(sb_leaderboard_entry_high_score(leaderboard.get(), 0, &highScore));
    REQUIRE(sb_leaderboard_entry_score_count(leaderboard.get(), 0, &scoreCount));
    REQUIRE(sb_leaderboard_entry_is_verified(leaderboard.get(), 0, &isVerified));
    REQUIRE(sb_leaderboard_entry_image(leaderboard.get(), 0, &image));
    REQUIRE(sb_leaderboard_entry_player_display_name(leaderboard.get(), 0, &displayName));
    REQUIRE(sb_leaderboard_entry_player_avatar(leaderboard.get(), 0, &avatar));
    REQUIRE(sb_leaderboard_entry_created(leaderboard.get(), 0, &created));

    CHECK(id == 3828382);
    CHECK(rank == 1);
    CHECK(highScore == 146250);
    CHECK(scoreCount == 12);
    CHECK(isVerified);
    CHECK(std::string(image) == "https://cdn-staging.scorbit.io/leaderboards/3828382.jpg");
    CHECK(std::string(displayName) == "Dilshod");
    CHECK(std::string(avatar) == "https://cdn-staging.scorbit.io/profile_pictures/dilshodm.jpg");
    CHECK(std::string(created) == "2026-03-31T08:14:10.091057Z");
}

TEST_CASE("Leaderboard C++ result converts from C helpers")
{
    LeaderboardPtr leaderboard(parseLeaderboardJson(sampleLeaderboardJson()),
                               &detail::destroyLeaderboard);
    REQUIRE(leaderboard);

    const auto result = LeaderboardResult::fromC(leaderboard.get());
    REQUIRE(result.entries.size() == 1);

    const auto &entry = result.entries.front();
    CHECK(entry.id == 3828382);
    CHECK(entry.rank == 1);
    CHECK(entry.highScore == 146250);
    CHECK(entry.image == "https://cdn-staging.scorbit.io/leaderboards/3828382.jpg");
    CHECK(entry.player.id == "016ae0a4-b1f8-7fc5-ba90-bd106d680829");
    CHECK(entry.player.username == "dilshodm");
    CHECK(entry.player.displayName == "Dilshod");
    CHECK(entry.player.avatarUrl == "https://cdn-staging.scorbit.io/profile_pictures/dilshodm.jpg");
    CHECK(entry.player.followerCount == 14);
    CHECK(entry.player.followingCount == 10);
    CHECK(entry.created == "2026-03-31T08:14:10.091057Z");
}

TEST_CASE("Leaderboard parser rejects malformed payloads")
{
    CHECK_FALSE(parseLeaderboardJson(R"({"not":"an array"})"));
    CHECK_FALSE(parseLeaderboardJson(R"([{"player":"not-an-object"}])"));
    CHECK_THROWS(parseLeaderboardJson("not json"));
}
