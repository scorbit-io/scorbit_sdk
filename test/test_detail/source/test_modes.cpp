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

TEST_CASE("Change modes")
{
    Modes modes;

    SECTION("Initially it's not changed")
    {
        CHECK(modes.isChanged() == false);
    }

    SECTION("Add mode")
    {
        // Adding a new mode should set changed flag to true
        modes.addMode("NA:Ball");
        REQUIRE(modes.isChanged() == true);

        // Clearing the changed state
        modes.clearChanged();
        CHECK(modes.isChanged() == false);

        // Adding the same mode again shouldn't change the state
        modes.addMode("NA:Ball");
        CHECK(modes.isChanged() == false);

        // Adding a different mode should change the state
        modes.addMode("NA:Multiball");
        CHECK(modes.isChanged() == true);
    }

    SECTION("Remove mode")
    {
        modes.addMode("NA:Ball");
        REQUIRE(modes.isChanged() == true);

        // Clearing the changed state
        modes.clearChanged();
        CHECK(modes.isChanged() == false);

        // Removing a non-existing mode shouldn't change the state
        modes.removeMode("NA:Multiball");
        CHECK(modes.isChanged() == false);

        // Removing an existing mode should change the state
        modes.removeMode("NA:Ball");
        CHECK(modes.isChanged() == true);
    }

    SECTION("Clear modes")
    {
        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        REQUIRE(modes.isChanged() == true);

        // Clearing the changed state
        modes.clearChanged();
        CHECK(modes.isChanged() == false);

        // Clearing all modes should change the state
        modes.clearModes();
        CHECK(modes.isChanged() == true);
    }
}

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

        modes.clearChanged();
        REQUIRE(modes.isChanged() == false);

        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        CHECK(modes.str() == "NA:Ball;NA:Multiball");
        CHECK(modes.isChanged() == false);
    }
}

TEST_CASE("Remove mode")
{
    Modes modes;
    modes.addMode("NA:Ball");
    modes.clearChanged();

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
        REQUIRE(modes.isChanged() == false);

        modes.removeMode("NA:NonExistent");
        CHECK(modes.str() == "NA:Ball");
        CHECK(modes.isChanged() == false);
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
        modes.clearChanged();
        REQUIRE(modes.isChanged() == false);

        modes.clearModes();
        CHECK(modes.str() == "");
        CHECK(modes.isChanged() == false);
    }

    SECTION("Clear modes when modes are present")
    {
        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        REQUIRE(modes.str() == "NA:Ball;NA:Multiball");

        modes.clearChanged();
        REQUIRE(modes.isChanged() == false);

        modes.clearModes();
        CHECK(modes.str() == "");
        CHECK(modes.isChanged() == true);
    }

    SECTION("Clear modes twice in a row only changes state once")
    {
        modes.addMode("NA:Ball");
        REQUIRE(modes.str() == "NA:Ball");

        modes.clearChanged();
        REQUIRE(modes.isChanged() == false);

        modes.clearModes();
        CHECK(modes.str() == "");
        CHECK(modes.isChanged() == true);

        // Clear again without adding any modes
        modes.clearChanged();
        REQUIRE(modes.isChanged() == false);

        modes.clearModes();
        CHECK(modes.str() == "");
        CHECK(modes.isChanged() == false);
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
        modes.clearModes();
        CHECK(modes.str() == "");
    }
}
