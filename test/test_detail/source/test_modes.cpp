/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#include <../source/modes.h>
#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("Add mode")
{
    Modes modes;

    SECTION("Add single mode")
    {
        modes.addMode("NA:Ball");
        CHECK(modes.str() == "NA:Ball");
    }

    SECTION("Add two modes")
    {
        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        CHECK(modes.str() == "NA:Ball;NA:Multiball");
    }

    SECTION("Adding duplicate modes doesn't affect state or content")
    {
        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");

        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        CHECK(modes.str() == "NA:Ball;NA:Multiball");
    }
}

TEST_CASE("Remove mode")
{
    Modes modes;
    modes.addMode("NA:Ball");

    SECTION("Remove single mode")
    {
        modes.removeMode("NA:Ball");
        CHECK(modes.str() == "");
    }

    SECTION("Remove one mode from multiple")
    {
        modes.addMode("NA:Multiball");
        REQUIRE(modes.str() == "NA:Ball;NA:Multiball");

        modes.removeMode("NA:Ball");
        CHECK(modes.str() == "NA:Multiball");
    }

    SECTION("Remove non-existent mode does not change state")
    {
        modes.removeMode("NA:NonExistent");
        CHECK(modes.str() == "NA:Ball");
    }

    SECTION("Remove all modes")
    {
        modes.addMode("NA:Multiball");
        modes.removeMode("NA:Ball");
        modes.removeMode("NA:Multiball");
        CHECK(modes.str() == "");
    }
}

TEST_CASE("Clear modes")
{
    Modes modes;

    SECTION("Clear modes when no modes are added does not change state")
    {
        modes.clear();
        CHECK(modes.str() == "");
    }

    SECTION("Clear modes when modes are present")
    {
        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        REQUIRE(modes.str() == "NA:Ball;NA:Multiball");

        modes.clear();
        CHECK(modes.str() == "");
    }

    SECTION("Clear modes twice in a row only changes state once")
    {
        modes.addMode("NA:Ball");
        REQUIRE(modes.str() == "NA:Ball");

        modes.clear();
        CHECK(modes.str() == "");

        modes.clear();
        CHECK(modes.str() == "");
    }
}

TEST_CASE("Modes to string")
{
    Modes modes;

    SECTION("Empty modes list returns an empty string")
    {
        CHECK(modes.str() == "");
    }

    SECTION("Single mode returns the mode name")
    {
        modes.addMode("NA:Ball");
        CHECK(modes.str() == "NA:Ball");
    }

    SECTION("Multiple modes concatenated with semicolons")
    {
        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        modes.addMode("NA:Superball");
        CHECK(modes.str() == "NA:Ball;NA:Multiball;NA:Superball");
    }

    SECTION("Adding duplicate modes does not change string")
    {
        modes.addMode("NA:Ball");
        modes.addMode("NA:Ball");
        CHECK(modes.str() == "NA:Ball");
    }

    SECTION("Removing all modes results in an empty string")
    {
        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        modes.clear();
        CHECK(modes.str() == "");
    }
}
