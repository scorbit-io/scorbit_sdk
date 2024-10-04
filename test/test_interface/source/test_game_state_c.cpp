/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/game_state_c.h>

#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

TEST_CASE("Create and destroy game state")
{
    sb_game_handle_t h = sb_create_game_state();
    REQUIRE(h != nullptr);

    sb_destroy_game_state(h);
}

