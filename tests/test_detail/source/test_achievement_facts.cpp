/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include "achievement_test_support.h"
#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit::detail::achievements;

using achievement_test::Timeline;

TEST_CASE("Mode activations are counted on absent-to-present transitions", "[achievements]")
{
    Timeline t;
    t.at(0).commit();
    t.at(1000).modes({"multiball"}).commit();
    t.at(2000).modes({"multiball", "wizard"}).commit(); // still present: not a new activation
    t.at(3000).modes({"wizard", "multiball"}).commit(); // reordered: not a new activation
    t.at(4000).modes({}).commit();
    t.at(5000).modes({"multiball"}).commit();

    const auto &p1 = t.facts.player(1);
    const auto activations = p1.activations("multiball");
    REQUIRE(activations.size() == 2);
    CHECK(activations[0].timeMs == 1000);
    CHECK(activations[1].timeMs == 5000);

    const auto durations = p1.activationDurations("multiball");
    REQUIRE(durations.size() == 2);
    CHECK(durations[0].ms == 3000);
    CHECK(durations[1].ms == 0); // running, measured up to the last row
}

TEST_CASE("A completion ends the activation and is recorded as an edge", "[achievements]")
{
    Timeline t;
    t.at(0).commit();
    t.at(1000).modes({"NA:Reactor"}).commit();
    t.at(2500).modes({}).completed({"NA:Reactor"}).commit();
    t.at(3000).commit();

    const auto &p1 = t.facts.player(1);
    REQUIRE(p1.completions("NA:Reactor").size() == 1);
    CHECK(p1.completions("NA:Reactor")[0].timeMs == 2500);
    REQUIRE(p1.activationDurations("NA:Reactor").size() == 1);
    CHECK(p1.activationDurations("NA:Reactor")[0].ms == 1500);
}

TEST_CASE("A running activation is closed at ball end with its real duration", "[achievements]")
{
    Timeline t;
    t.at(0).commit();
    t.at(1000).modes({"multiball"}).commit();
    t.at(61000).ball(2).commit(); // ball drained while multiball still listed
    t.at(62000).commit();

    const auto &p1 = t.facts.player(1);
    const auto durations = p1.activationDurations("multiball");
    REQUIRE(durations.size() == 1); // does not reopen on ball 2
    CHECK(durations[0].ms == 60000);
    CHECK(durations[0].ball == 1);
    CHECK(p1.activations("multiball").size() == 1);
}

TEST_CASE("Ball durations and multiplayer session time", "[achievements]")
{
    Timeline t;
    t.at(0).score(2, 0).commit();           // p1 ball 1
    t.at(10000).player(2).commit();         // p2 ball 1
    t.at(25000).player(1).ball(2).commit(); // p1 ball 2
    t.at(30000).player(2).commit();         // p2 ball 2
    t.facts.finish(32000);

    const auto &p1 = t.facts.player(1);
    const auto &p2 = t.facts.player(2);

    const auto p1Balls = p1.ballDurations();
    REQUIRE(p1Balls.size() == 2);
    CHECK(p1Balls[0].ms == 10000);
    CHECK(p1Balls[1].ms == 5000);
    CHECK(p1.sessionTimeMs() == 15000);

    const auto p2Balls = p2.ballDurations();
    REQUIRE(p2Balls.size() == 2);
    CHECK(p2Balls[0].ms == 15000);
    CHECK(p2Balls[1].ms == 2000);
    CHECK(p2.sessionTimeMs() == 17000);
}

TEST_CASE("Session time of the player on turn runs up to the last row", "[achievements]")
{
    Timeline t;
    t.at(1000).commit();
    t.at(4000).score(1, 10).commit();
    CHECK(t.facts.player(1).sessionTimeMs() == 3000);
}

TEST_CASE("Facts are attributed to the active player", "[achievements]")
{
    Timeline t;
    t.at(0).score(2, 0).commit();
    t.at(1000).event("spins", 3).commit();
    t.at(2000).player(2).event("spins", 5).modes({"multiball"}).commit();
    t.at(3000).completed({"multiball"}).modes({}).commit();

    CHECK(t.facts.player(1).eventOps("spins").size() == 1);
    CHECK(t.facts.player(1).activations("multiball").empty());
    REQUIRE(t.facts.player(2).eventOps("spins").size() == 1);
    CHECK(t.facts.player(2).eventOps("spins")[0].value == 5);
    CHECK(t.facts.player(2).activations("multiball").size() == 1);
    CHECK(t.facts.player(2).completions("multiball").size() == 1);
}

TEST_CASE("Event operations keep their values, including negative ones", "[achievements]")
{
    Timeline t;
    t.at(0).commit();
    t.at(1000).event("spins", 3).event("spins", -1).commit();
    t.at(2000).ball(2).event("spins", 10).commit();

    const auto ops = t.facts.player(1).eventOps("spins");
    REQUIRE(ops.size() == 3);
    CHECK(ops[0].value == 3);
    CHECK(ops[1].value == -1);
    CHECK(ops[2].value == 10);
    CHECK(ops[2].ball == 2);
    CHECK(ops[0].span != ops[2].span);
}

TEST_CASE("Mode stacks form when the last of their modes activates", "[achievements]")
{
    Timeline t;
    t.at(0).commit();
    t.at(1000).modes({"multiball"}).commit();
    t.at(2000).modes({"multiball", "wizard"}).commit(); // stack #1
    t.at(3000).modes({"multiball"}).commit();
    t.at(4000).modes({"multiball", "wizard"}).commit(); // stack #2
    t.at(5000).modes({}).commit();
    t.at(6000).modes({"wizard", "multiball"}).commit(); // both at once: stack #3

    const auto stacks = t.facts.stacks({"multiball", "wizard"}, 1);
    REQUIRE(stacks.size() == 3);
    CHECK(stacks[0].timeMs == 2000);
    CHECK(stacks[1].timeMs == 4000);
    CHECK(stacks[2].timeMs == 6000);
}

TEST_CASE("A stack counts once while it stays intact", "[achievements]")
{
    // The table of Stack Attack (§5.6)
    Timeline t;
    t.at(0).modes({"multiball"}).commit();
    t.at(1000).modes({"multiball", "wizard"}).commit(); // both active: 1
    t.at(2000).modes({"multiball", "wizard"}).commit(); // still stacked: no new occurrence
    t.at(3000).modes({"multiball"}).commit();           // wizard ended
    t.at(4000).modes({"multiball", "wizard"}).commit(); // the stack forms again: 2

    const auto stacks = t.facts.stacks({"multiball", "wizard"}, 1);
    REQUIRE(stacks.size() == 2);
    CHECK(stacks[0].timeMs == 1000);
    CHECK(stacks[1].timeMs == 4000);
}

TEST_CASE("A stack is judged on game_modes, not on how long a mode is timed", "[achievements]")
{
    SECTION("A stack intact across a drain is one occurrence")
    {
        Timeline t;
        t.at(0).modes({"multiball", "wizard"}).commit();
        t.at(1000).ball(2).commit(); // the ball ends, both modes carry over

        CHECK(t.facts.stacks({"multiball", "wizard"}, 1).size() == 1);
    }

    SECTION("A stack formed around a mode carried over a drain")
    {
        Timeline t;
        t.at(0).modes({"multiball"}).commit();
        t.at(1000).ball(2).commit();
        t.at(2000).modes({"multiball", "wizard"}).commit();

        const auto stacks = t.facts.stacks({"multiball", "wizard"}, 1);
        REQUIRE(stacks.size() == 1);
        CHECK(stacks[0].timeMs == 2000);
        CHECK(stacks[0].ball == 2);
    }

    SECTION("A stack formed around a completed mode still present")
    {
        Timeline t;
        t.at(0).modes({"multiball"}).commit();
        t.at(1000).completed({"multiball"}).commit();
        t.at(2000).modes({"multiball", "wizard"}).commit();

        CHECK(t.facts.stacks({"multiball", "wizard"}, 1).size() == 1);
    }
}

TEST_CASE("A stack belongs to the player on the row it formed", "[achievements]")
{
    Timeline t;
    t.at(0).modes({"multiball"}).commit();              // p1 starts multiball
    t.at(1000).player(2).commit();                      // it carries over to p2
    t.at(2000).modes({"multiball", "wizard"}).commit(); // p2 forms the stack

    CHECK(t.facts.stacks({"multiball", "wizard"}, 1).empty());
    const auto stacks = t.facts.stacks({"multiball", "wizard"}, 2);
    REQUIRE(stacks.size() == 1);
    CHECK(stacks[0].timeMs == 2000);

    // The carried-over mode is not a new activation for the next player (§4.4)
    CHECK(t.facts.player(1).activations("multiball").size() == 1);
    CHECK(t.facts.player(2).activations("multiball").empty());
}

TEST_CASE("Scores track every value the column took", "[achievements]")
{
    Timeline t;
    t.at(0).commit();
    t.at(1000).score(1, 500).commit();
    t.at(2000).score(1, 300).commit(); // e.g. a correction
    t.at(3000).score(1, 900).commit();

    const auto &p1 = t.facts.player(1);
    CHECK(p1.scoreValues() == std::set<int64_t> {0, 300, 500, 900});
    CHECK(p1.latestScore() == 900);
}

TEST_CASE("Invalid names and players beyond the timeline are ignored", "[achievements]")
{
    Timeline t;
    t.at(0).commit();
    t.at(1000).modes({"bad;name", "good"}).event("a=b", 1).commit();
    t.at(2000).player(7).modes({"good", "later"}).commit();

    CHECK(t.facts.player(1).activations("bad;name").empty());
    CHECK(t.facts.player(1).eventOps("a=b").empty());
    CHECK(t.facts.player(1).activations("good").size() == 1);
    CHECK(t.facts.player(7).activations("later").empty());
}

TEST_CASE("Durations never go negative when the clock steps back", "[achievements]")
{
    Timeline t;
    t.at(10000).commit();
    t.at(11000).modes({"multiball"}).commit();
    t.at(5000).modes({}).commit();

    const auto durations = t.facts.player(1).activationDurations("multiball");
    REQUIRE(durations.size() == 1);
    CHECK(durations[0].ms == 0);
}

TEST_CASE("The row that ends a ball reports whose ball ended", "[achievements]")
{
    Timeline t;
    t.at(0).score(2, 0).commit();
    CHECK_FALSE(t.facts.lastRowEndedBallOf());
    t.at(1000).score(1, 10).commit();
    CHECK_FALSE(t.facts.lastRowEndedBallOf());
    t.at(2000).player(2).commit();
    CHECK(t.facts.lastRowEndedBallOf() == 1u);
    t.facts.finish(3000);
    CHECK(t.facts.lastRowEndedBallOf() == 2u);
    CHECK(t.facts.finished());
}

TEST_CASE("A mode's score gain spans balls until the mode leaves", "[achievements]")
{
    Timeline t;
    t.at(0).score(1, 1000).modes({"multiball"}).commit();        // the activation row's own score
    t.at(1000).score(1, 5000).ball(2).commit();                  // the drain does not close it
    t.at(2000).score(1, 9000).completed({"multiball"}).commit(); // nor a completion it survives

    auto runs = t.facts.modeRuns("multiball", 1);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].open());
    CHECK(runs[0].gain == 8000);
    CHECK(runs[0].ball == 1);

    t.at(3000).score(1, 9500).modes({}).commit();
    runs = t.facts.modeRuns("multiball", 1);
    REQUIRE(runs.size() == 1);
    CHECK_FALSE(runs[0].open());
    CHECK(runs[0].endMs == 3000);
    CHECK(runs[0].gain == 8500);

    // The timed activation, by contrast, closed at the drain
    REQUIRE(t.facts.player(1).activationDurations("multiball").size() == 1);
    CHECK(t.facts.player(1).activationDurations("multiball")[0].ms == 1000);
}

TEST_CASE("A mode's score gain is its owner's alone", "[achievements]")
{
    Timeline t;
    t.at(0).score(1, 100).score(2, 0).modes({"multiball"}).commit(); // p1 starts it
    t.at(1000).player(2).score(2, 70'000).commit();                  // p2 plays on with it
    t.at(2000).score(1, 400).modes({}).commit();                     // it ends

    const auto runs = t.facts.modeRuns("multiball", 1);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].gain == 300);
    CHECK(t.facts.modeRuns("multiball", 2).empty());
}

TEST_CASE("A mode's score gain never goes below zero, and keeps its peak", "[achievements]")
{
    Timeline t;
    t.at(0).score(1, 1000).modes({"multiball"}).commit();
    t.at(1000).score(1, 6000).commit();
    t.at(2000).score(1, 500).modes({}).commit(); // e.g. a correction

    const auto runs = t.facts.modeRuns("multiball", 1);
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].gain == 0);
    CHECK(runs[0].peakGain == 5000);
}

TEST_CASE("The session's end closes a running mode", "[achievements]")
{
    Timeline t;
    t.at(0).modes({"multiball"}).commit();
    t.at(1000).score(1, 2500).commit();
    t.facts.finish(4000);

    const auto runs = t.facts.modeRuns("multiball", 1);
    REQUIRE(runs.size() == 1);
    CHECK_FALSE(runs[0].open());
    CHECK(runs[0].endMs == 4000);
    CHECK(runs[0].gain == 2500);
}

TEST_CASE("Synchronised periods are those the clock vouched for", "[achievements]")
{
    Timeline t;
    t.at(1000).commit();
    t.at(2000).commit();
    t.at(3000).synchronised(false).commit();
    t.at(4000).synchronised(true).commit();
    t.at(4000).commit(); // the same millisecond still vouches for its instant

    CHECK(t.facts.clockSynchronised());
    CHECK(t.facts.synchronisedPeriods().intervals()
          == std::vector<TimeInterval> {{1000, 3000}, {4000, 4001}});

    t.at(5000).synchronised(false).commit();
    CHECK_FALSE(t.facts.clockSynchronised());
    CHECK(t.facts.synchronisedPeriods().intervals()
          == std::vector<TimeInterval> {{1000, 3000}, {4000, 5000}});
}
