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

TEST_CASE("New created player score 0")
{
    PlayerState ps {1};
    CHECK(ps.player() == 1);
    CHECK(ps.score() == 0);
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

    ps.setScore(100);
    CHECK(ps.score() == 100);

    ps.setScore(200);
    CHECK(ps.score() == 200);
}
