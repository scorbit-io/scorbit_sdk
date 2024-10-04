/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/scorbit_sdk.h>
#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

TEST_CASE("version") {
    CHECK(std::string(SCORBIT_SDK_VERSION) == std::string("0.1.0"));
}

TEST_CASE("Net ctor") {
    scorbit::Net n;
}
