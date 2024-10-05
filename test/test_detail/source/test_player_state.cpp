/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include <../source/player_state.h>
#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("Change state")
{
    PlayerState ps {1};

    SECTION("Initially it's not changed")
    {
        CHECK(ps.isChanged() == false);
    }

    SECTION("Set score")
    {
        ps.setScore(100);
        CHECK(ps.isChanged() == true);

        ps.clearChanged();
        CHECK(ps.isChanged() == false);

        // Setting same score doesn't change the state
        ps.setScore(100);
        CHECK(ps.isChanged() == false);

        // Setting different score changes the state
        ps.setScore(200);
        CHECK(ps.isChanged() == true);
    }
}

TEST_CASE("Player number")
{
    PlayerState ps {1};
    CHECK(ps.player() == 1);

    PlayerState ps2 {2};
    CHECK(ps2.player() == 2);
}

TEST_CASE("Score")
{
    PlayerState ps {1};
    CHECK(ps.score() == -1);

    ps.setScore(100);
    CHECK(ps.score() == 100);

    ps.setScore(200);
    CHECK(ps.score() == 200);
}

