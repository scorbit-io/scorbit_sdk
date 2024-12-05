/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Dec 2024
 *
 ****************************************************************************/

#include "utils/mac_address.h"
#include <catch2/catch_test_macros.hpp>
#include <regex>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("Valid MAC address", "[getMacAddress]")
{
    static const auto re = std::regex("([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})");
    const auto mac = getMacAddress();

    CHECK(std::regex_match(mac, re));
}
