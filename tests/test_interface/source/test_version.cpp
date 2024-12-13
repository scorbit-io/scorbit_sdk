/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/version.h>
#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static


TEST_CASE("version")
{
    CHECK(std::string(SCORBIT_SDK_VERSION) == std::string("1.0.0"));
}
