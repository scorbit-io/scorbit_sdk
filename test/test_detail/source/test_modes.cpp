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
    ChangeTracker tracker;
    Modes modes {tracker};

    SECTION("Initially it's not changed")
    {
        CHECK(tracker.isChanged() == false);
    }

    SECTION("Add mode")
    {
        // Adding a new mode should set changed flag to true
        modes.addMode("NA:Ball");
        REQUIRE(tracker.isChanged() == true);

        // Clearing the changed state
        tracker.clearChanged();
        CHECK(tracker.isChanged() == false);

        // Adding the same mode again shouldn't change the state
        modes.addMode("NA:Ball");
        CHECK(tracker.isChanged() == false);

        // Adding a different mode should change the state
        modes.addMode("NA:Multiball");
        CHECK(tracker.isChanged() == true);
    }

    SECTION("Remove mode")
    {
        modes.addMode("NA:Ball");
        REQUIRE(tracker.isChanged() == true);

        // Clearing the changed state
        tracker.clearChanged();
        CHECK(tracker.isChanged() == false);

        // Removing a non-existing mode shouldn't change the state
        modes.removeMode("NA:Multiball");
        CHECK(tracker.isChanged() == false);

        // Removing an existing mode should change the state
        modes.removeMode("NA:Ball");
        CHECK(tracker.isChanged() == true);
    }

    SECTION("Clear modes")
    {
        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        REQUIRE(tracker.isChanged() == true);

        // Clearing the changed state
        tracker.clearChanged();
        CHECK(tracker.isChanged() == false);

        // Clearing all modes should change the state
        modes.clearModes();
        CHECK(tracker.isChanged() == true);
    }
}

TEST_CASE("Add mode")
{
    ChangeTracker tracker;
    Modes modes {tracker};

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

        tracker.clearChanged();
        REQUIRE(tracker.isChanged() == false);

        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        CHECK(modes.str() == "NA:Ball;NA:Multiball");
        CHECK(tracker.isChanged() == false);
    }
}

TEST_CASE("Remove mode")
{
    ChangeTracker tracker;
    Modes modes {tracker};
    modes.addMode("NA:Ball");
    tracker.clearChanged();

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
        REQUIRE(tracker.isChanged() == false);

        modes.removeMode("NA:NonExistent");
        CHECK(modes.str() == "NA:Ball");
        CHECK(tracker.isChanged() == false);
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
    ChangeTracker tracker;
    Modes modes{tracker};

    SECTION("Clear modes when no modes are added does not change state")
    {
        tracker.clearChanged();
        REQUIRE(tracker.isChanged() == false);

        modes.clearModes();
        CHECK(modes.str() == "");
        CHECK(tracker.isChanged() == false);
    }

    SECTION("Clear modes when modes are present")
    {
        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        REQUIRE(modes.str() == "NA:Ball;NA:Multiball");

        tracker.clearChanged();
        REQUIRE(tracker.isChanged() == false);

        modes.clearModes();
        CHECK(modes.str() == "");
        CHECK(tracker.isChanged() == true);
    }

    SECTION("Clear modes twice in a row only changes state once")
    {
        modes.addMode("NA:Ball");
        REQUIRE(modes.str() == "NA:Ball");

        tracker.clearChanged();
        REQUIRE(tracker.isChanged() == false);

        modes.clearModes();
        CHECK(modes.str() == "");
        CHECK(tracker.isChanged() == true);

        // Clear again without adding any modes
        tracker.clearChanged();
        REQUIRE(tracker.isChanged() == false);

        modes.clearModes();
        CHECK(modes.str() == "");
        CHECK(tracker.isChanged() == false);
    }
}

TEST_CASE("Modes to string")
{
    ChangeTracker tracker;
    Modes modes {tracker};

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
