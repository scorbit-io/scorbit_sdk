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

#include <nlohmann/json.hpp>
#include <catch2/catch_test_macros.hpp>
#include <trompeloeil.hpp>

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

static const std::string TEST_MACHINE_UUID = "test-machine-uuid-1234";

TEST_CASE("PlayerProfile 1 player")
{
    auto profiles = nlohmann::json::parse(R"(
        [
          {
            "id": 100,
            "position": 1,
            "player": {
              "id": "509fee4f-0137-4418-b289-149c7bcc6141",
              "username": "dilshodm",
              "display_name": "Dilshod M",
              "initials": "DTM",
              "prefer_initials": false,
              "avatar": "https://cdn-staging.scorbit.io/profile_pictures/dilshodm_TDrhEu1.jpg",
              "url": "https://staging.scorbit.io/api/v2/users/dilshodm/"
            }
          }
        ]
    )");

    PlayerProfilesManager pm;

    auto result = pm.setProfiles(profiles, TEST_MACHINE_UUID);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 1);

    auto p1 = pm.profile(1);
    REQUIRE(p1.has_value());
    CHECK(p1->hasInfo());
    CHECK(p1->id == "509fee4f-0137-4418-b289-149c7bcc6141");
    CHECK(p1->name == "Dilshod M");
    CHECK(p1->initials == "DTM");
    CHECK_FALSE(p1->preferInitials);
    CHECK(p1->pictureUrl == "https://cdn-staging.scorbit.io/profile_pictures/dilshodm_TDrhEu1.jpg");
    CHECK(p1->claimDeeplink.empty());

    // Check picture (keyed by avatar URL)
    const auto &avatarUrl = p1->pictureUrl;
    Picture picture {1, 2, 3};
    pm.setPicture(avatarUrl, Picture(picture));
    REQUIRE(pm.hasPicture(avatarUrl));

    auto p1Picture = pm.picture(avatarUrl);
    CHECK(p1Picture == picture);
}

TEST_CASE("PlayerProfile 2 players with unclaimed slot")
{
    auto profiles = nlohmann::json::parse(R"(
        [
          {
            "id": 200,
            "position": 1,
            "player": {
              "id": "509fee4f-0137-4418-b289-149c7bcc6141",
              "username": "dilshodm",
              "display_name": "Dilshod M",
              "initials": "DTM",
              "prefer_initials": false,
              "avatar": "https://cdn-staging.scorbit.io/profile_pictures/dilshodm_TDrhEu1.jpg",
              "url": "https://staging.scorbit.io/api/v2/users/dilshodm/"
            }
          },
          {
            "id": 201,
            "position": 2,
            "player": null
          }
        ]
    )");

    auto profiles2 = nlohmann::json::parse(R"(
        [
          {
            "id": 200,
            "position": 1,
            "player": {
              "id": "509fee4f-0137-4418-b289-149c7bcc6141",
              "username": "dilshodm",
              "display_name": "Dilshod M",
              "initials": "DTM",
              "prefer_initials": false,
              "avatar": "https://cdn-staging.scorbit.io/profile_pictures/dilshodm_TDrhEu1.jpg",
              "url": "https://staging.scorbit.io/api/v2/users/dilshodm/"
            }
          },
          {
            "id": 201,
            "position": 2,
            "player": {
              "id": "821eb8c8-e48f-4b71-84b2-7ae9382f0e60",
              "username": "dilshodm2",
              "display_name": "Dilshod M2",
              "initials": "DT2",
              "prefer_initials": true,
              "avatar": "https://cdn-staging.scorbit.io/profile_pictures/dilshodm2.jpg",
              "url": "https://staging.scorbit.io/api/v2/users/dilshodm2/"
            }
          }
        ]
    )");

    PlayerProfilesManager pm;

    auto result = pm.setProfiles(profiles, TEST_MACHINE_UUID);
    REQUIRE(result.has_value());
    REQUIRE(result->size() == 2);

    auto p2 = pm.profile(2);
    REQUIRE(p2.has_value());
    CHECK_FALSE(p2->hasInfo());
    CHECK(p2->claimDeeplink ==
          "https://scorbit.link/machines/test-machine-uuid-1234/?score_id=201");

    auto result2 = pm.setProfiles(profiles2, TEST_MACHINE_UUID);
    REQUIRE(result2.has_value());

    p2 = pm.profile(2);
    REQUIRE(p2.has_value());
    CHECK(p2->hasInfo());
    CHECK(p2->id == "821eb8c8-e48f-4b71-84b2-7ae9382f0e60");
    CHECK(p2->name == "Dilshod M2");
    CHECK(p2->initials == "DT2");
    CHECK(p2->preferInitials);
    CHECK(p2->pictureUrl == "https://cdn-staging.scorbit.io/profile_pictures/dilshodm2.jpg");
    CHECK(p2->claimDeeplink.empty());

    auto p3 = pm.profile(3);
    CHECK_FALSE(p3.has_value());

    // Cache is keyed by avatar URL: after caching p2's avatar, only p1's URL still needs download
    Picture picture {1, 2, 3};
    const std::string p2Avatar {"https://cdn-staging.scorbit.io/profile_pictures/dilshodm2.jpg"};
    pm.setPicture(p2Avatar, Picture(picture));
    const auto toDownload = pm.picturesToDownload();
    REQUIRE(toDownload.size() == 1);
    CHECK(toDownload.at(1) == "https://cdn-staging.scorbit.io/profile_pictures/dilshodm_TDrhEu1.jpg");
}

TEST_CASE("Player profile with null profile_picture")
{
    auto profiles = nlohmann::json::parse(R"(
        [
          {
            "id": 300,
            "position": 1,
            "player": {
              "id": "509fee4f-0137-4418-b289-149c7bcc6141",
              "username": "dilshodm",
              "display_name": "Dilshod M",
              "initials": "DTM",
              "prefer_initials": false,
              "avatar": null,
              "url": "https://staging.scorbit.io/api/v2/users/dilshodm/"
            }
          }
        ]
    )");

    PlayerProfilesManager pm;

    auto result = pm.setProfiles(profiles, TEST_MACHINE_UUID);
    REQUIRE(result.has_value());

    auto p1 = pm.profile(1);
    REQUIRE(p1.has_value());
    CHECK(p1->id == "509fee4f-0137-4418-b289-149c7bcc6141");
    CHECK(p1->pictureUrl.empty());
}

TEST_CASE("Player profile without profile_picture")
{
    auto profiles = nlohmann::json::parse(R"(
        [
          {
            "id": 400,
            "position": 1,
            "player": {
              "id": "509fee4f-0137-4418-b289-149c7bcc6141",
              "username": "dilshodm",
              "display_name": "Dilshod M",
              "initials": "DTM",
              "prefer_initials": false,
              "url": "https://staging.scorbit.io/api/v2/users/dilshodm/"
            }
          }
        ]
    )");

    PlayerProfilesManager pm;

    auto result = pm.setProfiles(profiles, TEST_MACHINE_UUID);
    REQUIRE(result.has_value());

    auto p1 = pm.profile(1);
    REQUIRE(p1.has_value());
    CHECK(p1->id == "509fee4f-0137-4418-b289-149c7bcc6141");
}

TEST_CASE("No change returns nullopt")
{
    auto profiles = nlohmann::json::parse(R"(
        [
          {
            "id": 500,
            "position": 1,
            "player": {
              "id": "509fee4f-0137-4418-b289-149c7bcc6141",
              "username": "dilshodm",
              "display_name": "Dilshod M",
              "initials": "DTM",
              "prefer_initials": false,
              "avatar": "https://cdn-staging.scorbit.io/profile_pictures/dilshodm_TDrhEu1.jpg",
              "url": "https://staging.scorbit.io/api/v2/users/dilshodm/"
            }
          }
        ]
    )");

    PlayerProfilesManager pm;

    auto result = pm.setProfiles(profiles, TEST_MACHINE_UUID);
    REQUIRE(result.has_value());

    auto result2 = pm.setProfiles(profiles, TEST_MACHINE_UUID);
    CHECK_FALSE(result2.has_value());
}
