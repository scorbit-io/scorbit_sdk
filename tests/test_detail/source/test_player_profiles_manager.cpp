/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * May 2025
 *
 ****************************************************************************/

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

    auto p3 = pm.profile(3);
    CHECK(p3 == nullptr);

    // Check pictures to download. We will set p1 picture, so only p2 has to be downloaded
    Picture picture {1, 2, 3};
    pm.setPicture(1, Picture(picture));
    const auto toDownload = pm.picturesToDownload();
    REQUIRE(toDownload.size() == 1);
    CHECK(toDownload.at(2) == "https://cdn-staging.scorbit.io/profile_pictures/dilshodm2.jpg");
}
