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

#include <boost/json.hpp>
#include <catch2/catch_test_macros.hpp>
#include <trompeloeil.hpp>

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

TEST_CASE("PlayerProfile 1 player")
{
    auto profiles = boost::json::parse(R"(
        [
          {
            "position": 1,
            "player": {
              "id": 47,
              "cached_display_name": "dilshodm",
              "initials": "DTM",
              "prefer_initials": false,
              "profile_picture": "https://cdn-staging.scorbit.io/profile_pictures/dilshodm_TDrhEu1.jpg"
            }
          }
        ]
    )");

    PlayerProfilesManager pm;

    pm.setProfiles(profiles);
    bool hasUpdate = pm.hasUpdate();
    REQUIRE(hasUpdate);

    auto p1 = pm.profile(1);
    REQUIRE(p1 != nullptr);
    CHECK(p1->id == 47);
    CHECK(p1->name == "dilshodm");
    CHECK(p1->initials == "DTM");
    CHECK_FALSE(p1->preferInitials);
    CHECK(p1->pictureUrl == "https://cdn-staging.scorbit.io/profile_pictures/dilshodm_TDrhEu1.jpg");

    // Check picture
    Picture picture {1, 2, 3};
    pm.setPicture(1, Picture(picture));
    REQUIRE(pm.hasPicture(1));

    auto p1Picture = pm.picture(1);
    CHECK(p1Picture == picture);
}

TEST_CASE("PlayerProfile 2 players")
{
    auto profiles = boost::json::parse(R"(
        [
          {
            "position": 1,
            "player": {
              "id": 47,
              "cached_display_name": "dilshodm",
              "initials": "DTM",
              "prefer_initials": false,
              "profile_picture": "https://cdn-staging.scorbit.io/profile_pictures/dilshodm_TDrhEu1.jpg"
            }
          },
          {
            "position": 2,
            "player": null
          }
        ]
    )");

    auto profiles2 = boost::json::parse(R"(
        [
          {
            "position": 1,
            "player": {
              "id": 47,
              "cached_display_name": "dilshodm",
              "initials": "DTM",
              "prefer_initials": false,
              "profile_picture": "https://cdn-staging.scorbit.io/profile_pictures/dilshodm_TDrhEu1.jpg"
            }
          },
          {
            "position": 2,
            "player": {
              "id": 48,
              "cached_display_name": "dilshodm2",
              "initials": "DT2",
              "prefer_initials": true,
              "profile_picture": "https://cdn-staging.scorbit.io/profile_pictures/dilshodm2.jpg"
            }
          }
        ]
    )");

    PlayerProfilesManager pm;

    pm.setProfiles(profiles);
    bool hasUpdate = pm.hasUpdate();
    REQUIRE(hasUpdate);

    auto p2 = pm.profile(2);
    CHECK(p2 == nullptr);

    pm.setProfiles(profiles2);
    hasUpdate = pm.hasUpdate();
    REQUIRE(hasUpdate);

    p2 = pm.profile(2);
    REQUIRE(p2 != nullptr);
    CHECK(p2->id == 48);
    CHECK(p2->name == "dilshodm2");
    CHECK(p2->initials == "DT2");
    CHECK(p2->preferInitials);
    CHECK(p2->pictureUrl == "https://cdn-staging.scorbit.io/profile_pictures/dilshodm2.jpg");

    auto p3 = pm.profile(3);
    CHECK(p3 == nullptr);

    // Check pictures to download. We will set p1 picture, so only p2 has to be downloaded
    Picture picture {1, 2, 3};
    pm.setPicture(1, Picture(picture));
    const auto toDownload = pm.picturesToDownload();
    REQUIRE(toDownload.size() == 1);
    CHECK(toDownload.at(2) == "https://cdn-staging.scorbit.io/profile_pictures/dilshodm2.jpg");
}

TEST_CASE("Player profile with null profile_picture")
{
    auto profiles = boost::json::parse(R"(
        [
          {
            "position": 1,
            "player": {
              "id": 47,
              "cached_display_name": "dilshodm",
              "initials": "DTM",
              "prefer_initials": false,
              "profile_picture": null
            }
          }
        ]
    )");

    PlayerProfilesManager pm;

    pm.setProfiles(profiles);
    bool hasUpdate = pm.hasUpdate();
    REQUIRE(hasUpdate);

    auto p1 = pm.profile(1);
    REQUIRE(p1 != nullptr);
    CHECK(p1->id == 47);
}

TEST_CASE("Player profile without profile_picture")
{
    auto profiles = boost::json::parse(R"(
        [
          {
            "position": 1,
            "player": {
              "id": 47,
              "cached_display_name": "dilshodm",
              "initials": "DTM",
              "prefer_initials": false
            }
          }
        ]
    )");

    PlayerProfilesManager pm;

    pm.setProfiles(profiles);
    bool hasUpdate = pm.hasUpdate();
    REQUIRE(hasUpdate);

    auto p1 = pm.profile(1);
    REQUIRE(p1 != nullptr);
    CHECK(p1->id == 47);
}
