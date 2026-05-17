/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <../source/modes.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

TEST_CASE("Add mode", "[Modes]")
{
    Modes modes;
    REQUIRE(modes.isEmpty());

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

TEST_CASE("addOrPromoteToFront", "[Modes]")
{
    Modes modes;
    modes.addMode("A");
    modes.addMode("B");
    CHECK(modes.str() == "A;B");

    modes.addOrPromoteToFront("B");
    CHECK(modes.str() == "B;A");
}

TEST_CASE("Remove mode", "[Modes]")
{
    Modes modes;
    modes.addMode("NA:Ball");

    SECTION("Remove single mode")
    {
        modes.removeMode("NA:Ball");
        CHECK(modes.isEmpty());
        CHECK(modes.str().empty());
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
        CHECK(modes.str().empty());
    }
}

TEST_CASE("Clear modes", "[Modes]")
{
    Modes modes;

    SECTION("Clear modes when no modes are added does not change state")
    {
        modes.clear();
        CHECK(modes.str().empty());
    }

    SECTION("Clear modes when modes are present")
    {
        modes.addMode("NA:Ball");
        modes.addMode("NA:Multiball");
        REQUIRE(modes.str() == "NA:Ball;NA:Multiball");

        modes.clear();
        CHECK(modes.str().empty());
    }

    SECTION("Clear modes twice in a row only changes state once")
    {
        modes.addMode("NA:Ball");
        REQUIRE(modes.str() == "NA:Ball");

        modes.clear();
        CHECK(modes.str().empty());

        modes.clear();
        CHECK(modes.str().empty());
    }
}

TEST_CASE("Modes to string", "[Modes]")
{
    Modes modes;

    SECTION("Empty modes list returns an empty string")
    {
        CHECK(modes.str().empty());
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
        CHECK(modes.str().empty());
    }
}

TEST_CASE("addModeExpiring - duration normalization", "[Modes]")
{
    auto approxEqMs = [](std::chrono::steady_clock::duration d, int expectedSec) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
        const auto want = static_cast<long long>(expectedSec) * 1000;
        const auto diff = ms - want;
        return diff <= 15 && diff >= -15;
    };

    SECTION("0 seconds normalizes to 3 seconds")
    {
        Modes modes;
        modes.addModeExpiring("MB:Multiball", 0);
        REQUIRE(modes.hasExpiryDeadlines());
        auto delay = modes.nextExpiryDelay();
        REQUIRE(delay.has_value());
        REQUIRE(approxEqMs(*delay, 3));
    }

    SECTION("Values above 10 clamp to 10 seconds")
    {
        Modes modes;
        modes.addModeExpiring("MB:Multiball", 100);
        auto delay = modes.nextExpiryDelay();
        REQUIRE(delay.has_value());
        REQUIRE(approxEqMs(*delay, 10));
    }

    SECTION("1-10 seconds pass through unchanged")
    {
        Modes modes;
        modes.addModeExpiring("MB:Multiball", 7);
        auto delay = modes.nextExpiryDelay();
        REQUIRE(delay.has_value());
        REQUIRE(approxEqMs(*delay, 7));
    }
}

TEST_CASE("addModeExpiring - promotes to front", "[Modes]")
{
    Modes modes;
    modes.addMode("A");
    modes.addMode("B");
    CHECK(modes.str() == "A;B");

    modes.addModeExpiring("B", 2);
    CHECK(modes.str() == "B;A");
    CHECK(modes.hasExpiryDeadlines());
}

TEST_CASE("tickExpiries - removes expired modes", "[Modes]")
{
    using namespace std::chrono_literals;

    Modes modes;
    modes.addModeExpiring("MB:Multiball", 1);
    REQUIRE(modes.contains("MB:Multiball"));
    REQUIRE(modes.hasExpiryDeadlines());

    std::this_thread::sleep_for(1100ms);

    modes.tickExpiries();
    CHECK_FALSE(modes.contains("MB:Multiball"));
    CHECK(modes.isEmpty());
    CHECK_FALSE(modes.hasExpiryDeadlines());
}

TEST_CASE("removeMode - clears deadline", "[Modes]")
{
    Modes modes;
    modes.addModeExpiring("MB:Multiball", 3);
    REQUIRE(modes.hasExpiryDeadlines());

    modes.removeMode("MB:Multiball");
    CHECK_FALSE(modes.hasExpiryDeadlines());
    CHECK(modes.isEmpty());
}

TEST_CASE("clear - clears deadlines", "[Modes]")
{
    Modes modes;
    modes.addModeExpiring("A", 2);
    modes.addModeExpiring("B", 3);
    REQUIRE(modes.hasExpiryDeadlines());

    modes.clear();
    CHECK_FALSE(modes.hasExpiryDeadlines());
    CHECK(modes.isEmpty());
}

TEST_CASE("clearExpiries - clears deadlines but keeps modes", "[Modes]")
{
    Modes modes;
    modes.addModeExpiring("A", 2);
    modes.addMode("B");
    REQUIRE(modes.hasExpiryDeadlines());
    REQUIRE(modes.str() == "A;B");

    modes.clearExpiries();
    CHECK_FALSE(modes.hasExpiryDeadlines());
    CHECK(modes.str() == "A;B");
}

TEST_CASE("nextExpiryDelay - returns nullopt when no expiring modes", "[Modes]")
{
    Modes modes;
    CHECK_FALSE(modes.nextExpiryDelay().has_value());

    modes.addMode("A");
    CHECK_FALSE(modes.nextExpiryDelay().has_value());
}

TEST_CASE("contains", "[Modes]")
{
    Modes modes;
    CHECK_FALSE(modes.contains("X"));

    modes.addMode("A");
    CHECK(modes.contains("A"));
    CHECK_FALSE(modes.contains("B"));

    modes.addMode("B");
    CHECK(modes.contains("A"));
    CHECK(modes.contains("B"));
    CHECK_FALSE(modes.contains("C"));

    modes.removeMode("A");
    CHECK_FALSE(modes.contains("A"));
    CHECK(modes.contains("B"));
}

TEST_CASE("jsonStr", "[Modes]")
{
    Modes modes;
    CHECK(modes.jsonStr() == "[]");

    modes.addMode("NA:Ball");
    CHECK(modes.jsonStr() == "[\"NA:Ball\"]");

    modes.addMode("NA:Multiball");
    CHECK(modes.jsonStr() == "[\"NA:Ball\",\"NA:Multiball\"]");

    modes.clear();
    CHECK(modes.jsonStr() == "[]");
}

TEST_CASE("operator==", "[Modes]")
{
    Modes a;
    Modes b;
    CHECK(a == b);

    a.addMode("X");
    CHECK_FALSE(a == b);
    b.addMode("X");
    CHECK(a == b);

    a.addMode("Y");
    CHECK_FALSE(a == b);
    b.addMode("Y");
    CHECK(a == b);

    a.addOrPromoteToFront("Y");
    CHECK_FALSE(a == b);
    b.addOrPromoteToFront("Y");
    CHECK(a == b);
}

TEST_CASE("addOrPromoteToFront - mode not in list is inserted at front", "[Modes]")
{
    Modes modes;
    modes.addMode("A");
    modes.addMode("B");
    CHECK(modes.str() == "A;B");

    modes.addOrPromoteToFront("C");
    CHECK(modes.str() == "C;A;B");
    CHECK(modes.contains("C"));
}

TEST_CASE("addModeExpiring - duration 10 unchanged, 11 clamps to 10", "[Modes]")
{
    auto approxEqMs = [](std::chrono::steady_clock::duration d, int expectedSec) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
        const auto want = static_cast<long long>(expectedSec) * 1000;
        const auto diff = ms - want;
        return diff <= 15 && diff >= -15;
    };

    Modes modes10;
    modes10.addModeExpiring("M", 10);
    REQUIRE(modes10.nextExpiryDelay().has_value());
    CHECK(approxEqMs(*modes10.nextExpiryDelay(), 10));

    Modes modes11;
    modes11.addModeExpiring("M", 11);
    REQUIRE(modes11.nextExpiryDelay().has_value());
    CHECK(approxEqMs(*modes11.nextExpiryDelay(), 10));
}

TEST_CASE("nextExpiryDelay - minimum of multiple deadlines", "[Modes]")
{
    auto approxEqMs = [](std::chrono::steady_clock::duration d, int expectedSec) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
        const auto want = static_cast<long long>(expectedSec) * 1000;
        const auto diff = ms - want;
        return diff <= 50 && diff >= -50;
    };

    Modes modes;
    modes.addModeExpiring("Long", 10);
    modes.addModeExpiring("Short", 2);

    auto delay = modes.nextExpiryDelay();
    REQUIRE(delay.has_value());
    CHECK(approxEqMs(*delay, 2));
}

TEST_CASE("tickExpiries - removes only expired modes", "[Modes]")
{
    using namespace std::chrono_literals;

    Modes modes;
    modes.addModeExpiring("Fast", 1);
    modes.addModeExpiring("Slow", 10);
    REQUIRE(modes.contains("Fast"));
    REQUIRE(modes.contains("Slow"));
    // Last expiring add is promoted to front
    REQUIRE(modes.str() == "Slow;Fast");

    std::this_thread::sleep_for(1100ms);

    modes.tickExpiries();
    CHECK_FALSE(modes.contains("Fast"));
    CHECK(modes.contains("Slow"));
    CHECK(modes.str() == "Slow");
    CHECK(modes.hasExpiryDeadlines());
}

TEST_CASE("tickExpiries - multiple expired modes in one tick", "[Modes]")
{
    using namespace std::chrono_literals;

    Modes modes;
    modes.addModeExpiring("A", 1);
    modes.addModeExpiring("B", 1);
    std::this_thread::sleep_for(1100ms);
    modes.tickExpiries();
    CHECK(modes.isEmpty());
    CHECK_FALSE(modes.hasExpiryDeadlines());
}

TEST_CASE("removeMode - non-expiring mode unchanged deadlines for others", "[Modes]")
{
    Modes modes;
    modes.addMode("Plain");
    modes.addModeExpiring("Exp", 5);
    REQUIRE(modes.hasExpiryDeadlines());
    modes.removeMode("Plain");
    CHECK(modes.hasExpiryDeadlines());
    CHECK(modes.contains("Exp"));
    CHECK_FALSE(modes.contains("Plain"));
}
