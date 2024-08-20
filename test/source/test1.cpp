/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Aug 2024
 *
 ****************************************************************************/

#include <scorbit_sdk/greeter.h>
#include <scorbit_sdk/version.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Version") {
    static_assert(std::string_view(SCORBIT_SDK_VERSION) == std::string_view("0.1.0"));
    CHECK(std::string(SCORBIT_SDK_VERSION) == std::string("0.1.0"));
}
