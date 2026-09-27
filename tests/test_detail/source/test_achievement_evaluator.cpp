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
#include <../source/achievements/rule_evaluator.h>
#include <catch2/catch_test_macros.hpp>

// clazy:excludeall=non-pod-global-static

using namespace achievement_test;

namespace {

constexpr auto GE = Comparison::Ge;
constexpr auto LE = Comparison::Le;
constexpr auto EQ = Comparison::Eq;

/** Evaluates achievements for player 1 of a timeline. */
struct Evaluator {
    Timeline timeline;
    DefinitionSet definitions;
    HeldSet held;
    Baselines baselines;

    Evaluation operator()(const Definition &def) const
    {
        const auto it = baselines.find(def.key);
        const EvaluationContext context {timeline.facts.player(1),
                                         it == baselines.end() ? nullptr : &it->second, held,
                                         definitions};
        return evaluate(def, context);
    }
};

} // namespace

TEST_CASE("Billionaires Club: lifetime score", "[achievements]")
{
    const auto def = unlimited("game-bop-billionaires", {rule(RuleType::Score, GE, 1'000'000'000)});
    Evaluator ev;
    ev.timeline.at(0).commit();

    SECTION("Crossed this session")
    {
        ev.timeline.at(1000).score(1, 1'200'000'000).commit();
        const auto result = ev(def);
        CHECK(result.allSatisfied);
        CHECK(result.rules.at(0) == RuleProgress {1'200'000'000, true});
    }

    SECTION("Crossed in an earlier session")
    {
        ev.baselines["game-bop-billionaires"].rules[0] = {1'500'000'000, true};
        ev.timeline.at(1000).score(1, 10).commit();
        const auto result = ev(def);
        CHECK(result.allSatisfied);
        CHECK(result.rules.at(0) == RuleProgress {1'500'000'000, true});
    }

    SECTION("Not yet")
    {
        ev.timeline.at(1000).score(1, 999'999'999).commit();
        CHECK_FALSE(ev(def).allSatisfied);
    }
}

TEST_CASE("Grand Finale: complete the mode", "[achievements]")
{
    const auto def = inSession("game-tom-finale",
                               {rule(RuleType::ModeCompleted, GE, 1, "Grand Finale")});
    Evaluator ev;
    ev.timeline.at(0).commit();
    ev.timeline.at(1000).modes({"Grand Finale"}).commit();
    CHECK_FALSE(ev(def).allSatisfied); // started is not completed

    ev.timeline.at(2000).modes({}).completed({"Grand Finale"}).commit();
    CHECK(ev(def).allSatisfied);
}

TEST_CASE("Marathon Multiball: one activation for sixty seconds", "[achievements]")
{
    const auto def = inSession("game-marathon", {rule(RuleType::TimerMode, GE, 60000, "multiball")});
    Evaluator ev;
    ev.timeline.at(0).commit();
    ev.timeline.at(1000).modes({"multiball"}).commit();
    ev.timeline.at(59000).score(1, 10).commit();
    CHECK_FALSE(ev(def).allSatisfied);

    // Still running: judged as soon as it has lasted long enough
    ev.timeline.at(61000).score(1, 20).commit();
    const auto result = ev(def);
    CHECK(result.allSatisfied);
    CHECK(result.rules.at(0).value == 60000);
}

TEST_CASE("Double Trouble: two multiballs within thirty seconds", "[achievements]")
{
    const auto def = inSession("game-double-trouble", {rule(RuleType::Mode, GE, 2, "multiball"),
                                                       rule(RuleType::TimerBetween, LE, 30000)});
    Evaluator ev;
    ev.timeline.at(0).commit();
    ev.timeline.at(1000).modes({"multiball"}).commit();
    ev.timeline.at(5000).modes({}).commit();

    SECTION("Close together")
    {
        ev.timeline.at(21000).modes({"multiball"}).commit();
        const auto result = ev(def);
        CHECK(result.allSatisfied);
        CHECK(result.rules.at(1) == RuleProgress {20000, true});
    }

    SECTION("Too far apart")
    {
        ev.timeline.at(41000).modes({"multiball"}).commit();
        const auto result = ev(def);
        CHECK_FALSE(result.allSatisfied);
        CHECK(result.rules.at(0).satisfied);
        CHECK(result.rules.at(1) == RuleProgress {40000, false});
    }

    SECTION("On different balls")
    {
        ev.timeline.at(10000).ball(2).commit();
        ev.timeline.at(12000).modes({"multiball"}).commit();
        const auto result = ev(def);
        CHECK(result.rules.at(0).satisfied);
        CHECK_FALSE(result.rules.at(1).satisfied);
    }
}

TEST_CASE("Quick Hands: two different facts close together, either order", "[achievements]")
{
    const auto def = inSession("game-quick-hands",
                               {rule(RuleType::Mode, GE, 1, "multiball"),
                                rule(RuleType::ModeCompleted, GE, 1, "ramp combo"),
                                rule(RuleType::TimerBetween, LE, 180000)});
    Evaluator ev;
    ev.timeline.at(0).commit();

    SECTION("Combo first")
    {
        ev.timeline.at(1000).completed({"ramp combo"}).commit();
        ev.timeline.at(100000).modes({"multiball"}).commit();
        const auto result = ev(def);
        CHECK(result.allSatisfied);
        CHECK(result.rules.at(2).value == 99000);
    }

    SECTION("The tightest window counts")
    {
        ev.timeline.at(1000).modes({"multiball"}).commit();
        ev.timeline.at(2000).modes({}).commit();
        ev.timeline.at(300000).modes({"multiball"}).commit();
        ev.timeline.at(310000).completed({"ramp combo"}).commit();
        const auto result = ev(def);
        CHECK(result.allSatisfied);
        CHECK(result.rules.at(2).value == 10000);
    }

    SECTION("Only one of the two happened: no span")
    {
        ev.timeline.at(1000).modes({"multiball"}).commit();
        const auto result = ev(def);
        CHECK_FALSE(result.allSatisfied);
        CHECK(result.rules.at(2) == RuleProgress {0, false}); // qualifier: 0 does not satisfy LE
    }
}

TEST_CASE("Ball One Hero: BALL filters sibling occurrences", "[achievements]")
{
    Evaluator ev;
    ev.timeline.at(0).commit();

    SECTION("Multiball on ball one")
    {
        const auto def = inSession("game-ball-one", {rule(RuleType::Mode, GE, 1, "multiball"),
                                                     rule(RuleType::Ball, EQ, 1)});
        ev.timeline.at(1000).modes({"multiball"}).commit();
        const auto result = ev(def);
        CHECK(result.allSatisfied);
        CHECK(result.rules.at(1) == RuleProgress {1, true});
    }

    SECTION("Multiball only on ball two")
    {
        const auto def = inSession("game-ball-one", {rule(RuleType::Mode, GE, 1, "multiball"),
                                                     rule(RuleType::Ball, EQ, 1)});
        ev.timeline.at(1000).ball(2).commit();
        ev.timeline.at(2000).modes({"multiball"}).commit();
        const auto result = ev(def);
        CHECK_FALSE(result.allSatisfied);
        CHECK(result.rules.at(0) == RuleProgress {0, false});
        CHECK(result.rules.at(1) == RuleProgress {0, false});
    }

    SECTION("Two multiballs on ball one means both")
    {
        const auto def = inSession("game-ball-one-twice", {rule(RuleType::Mode, GE, 2, "multiball"),
                                                           rule(RuleType::Ball, EQ, 1)});
        ev.timeline.at(1000).modes({"multiball"}).commit();
        ev.timeline.at(2000).modes({}).ball(3).commit();
        ev.timeline.at(3000).modes({"multiball"}).commit();
        const auto result = ev(def);
        CHECK_FALSE(result.allSatisfied);
        CHECK(result.rules.at(0) == RuleProgress {1, false});
    }
}

TEST_CASE("Iron Wrist: one ball lasting five minutes", "[achievements]")
{
    const auto def = inSession("game-iron-wrist", {rule(RuleType::TimerBall, GE, 300000)});
    Evaluator ev;
    ev.timeline.at(0).commit();
    ev.timeline.at(200000).ball(2).commit();
    CHECK_FALSE(ev(def).allSatisfied);
    ev.timeline.at(500000).ball(3).commit();
    CHECK(ev(def).allSatisfied);
}

TEST_CASE("The Long Haul: any session of thirty minutes", "[achievements]")
{
    const auto def = unlimited("game-long-haul", {rule(RuleType::TimerSession, GE, 1'800'000)});
    Evaluator ev;
    ev.timeline.at(0).commit();
    ev.timeline.at(60000).score(1, 1).commit();

    SECTION("Short session, no history")
    {
        CHECK(ev(def).rules.at(0) == RuleProgress {60000, false});
    }

    SECTION("An earlier session was long enough")
    {
        ev.baselines["game-long-haul"].rules[0] = {1'900'000, true};
        CHECK(ev(def).rules.at(0) == RuleProgress {1'900'000, true});
    }
}

TEST_CASE("First try: complete a mode on its only activation", "[achievements]")
{
    const auto def = inSession("game-first-try",
                               {rule(RuleType::Mode, EQ, 1, "Grand Finale"),
                                rule(RuleType::ModeCompleted, GE, 1, "Grand Finale")});
    Evaluator ev;
    ev.timeline.at(0).commit();
    ev.timeline.at(1000).modes({"Grand Finale"}).commit();

    SECTION("Completed first time")
    {
        ev.timeline.at(2000).modes({}).completed({"Grand Finale"}).commit();
        CHECK(ev(def).allSatisfied);
    }

    SECTION("Restarted before completing")
    {
        ev.timeline.at(2000).modes({}).commit();
        ev.timeline.at(3000).modes({"Grand Finale"}).commit();
        ev.timeline.at(4000).modes({}).completed({"Grand Finale"}).commit();
        CHECK_FALSE(ev(def).allSatisfied);
    }
}

TEST_CASE("Centurion: the game in progress counts", "[achievements]")
{
    const auto def = unlimited("game-centurion", {rule(RuleType::Session, GE, 100)});
    Evaluator ev;
    ev.timeline.at(0).commit();

    SECTION("First game ever resolves to one")
    {
        CHECK(ev(def).rules.at(0) == RuleProgress {1, false});
    }

    SECTION("Hundredth game is decided during it")
    {
        ev.baselines["game-centurion"].rules[0] = {99, false};
        CHECK(ev(def).rules.at(0) == RuleProgress {100, true});
    }
}

TEST_CASE("Spinner Master: event register accumulates for life", "[achievements]")
{
    const auto def = unlimited("game-spinner-master", {rule(RuleType::Event, GE, 10000, "spins")});
    Evaluator ev;
    ev.baselines["game-spinner-master"].rules[0] = {9990, false};
    ev.timeline.at(0).commit();
    ev.timeline.at(1000).event("spins", 7).commit();
    CHECK(ev(def).rules.at(0) == RuleProgress {9997, false});
    ev.timeline.at(2000).event("spins", 5).event("spins", -2).commit();
    CHECK(ev(def).rules.at(0) == RuleProgress {10000, true});
}

TEST_CASE("An in_session window ignores the stored value", "[achievements]")
{
    const auto def = inSession("game-ten-spins", {rule(RuleType::Event, GE, 10, "spins")});
    Evaluator ev;
    ev.baselines["game-ten-spins"].rules[0] = {9, false};
    ev.timeline.at(0).commit();
    ev.timeline.at(1000).event("spins", 3).commit();
    CHECK(ev(def).rules.at(0) == RuleProgress {3, false});
}

TEST_CASE("Stack Attack: two modes together, three times", "[achievements]")
{
    const auto def = inSession("game-stack-attack",
                               {rule(RuleType::ModeStack, GE, 3, "multiball,wizard")});
    Evaluator ev;
    ev.timeline.at(0).commit();
    for (int i = 0; i < 3; ++i) {
        ev.timeline.at(1000 + i * 3000).modes({"multiball"}).commit();
        ev.timeline.at(2000 + i * 3000).modes({"multiball", "wizard"}).commit();
        ev.timeline.at(3000 + i * 3000).modes({}).commit();
    }
    CHECK(ev(def).rules.at(0) == RuleProgress {3, true});
}

TEST_CASE("LE rules are not paired implicitly (§6.3)", "[achievements]")
{
    Evaluator ev;
    ev.timeline.at(0).commit();
    ev.timeline.at(190000).score(1, 1).commit(); // a long game, gem X never triggered

    SECTION("The wrong rule set passes for a player who did nothing about gem X")
    {
        const auto wrong = inSession("game-taf-gem-wrong",
                                     {rule(RuleType::TimerMode, LE, 60000, "gemX"),
                                      rule(RuleType::TimerSession, GE, 180000)});
        const auto result = ev(wrong);
        CHECK(result.allSatisfied);
        CHECK(result.rules.at(0) == RuleProgress {0, true});
    }

    SECTION("The existence rule makes it correct")
    {
        const auto right = inSession("game-taf-gem",
                                     {rule(RuleType::Mode, GE, 1, "gemX"),
                                      rule(RuleType::TimerMode, LE, 60000, "gemX"),
                                      rule(RuleType::TimerSession, GE, 180000)});
        CHECK_FALSE(ev(right).allSatisfied);
    }
}

TEST_CASE("ACHIEVEMENT rules", "[achievements]")
{
    const auto parent = unlimited("game-tom-devotee",
                                  {rule(RuleType::Achievement, GE, 1, "game-tom-tenball"),
                                   rule(RuleType::Achievement, GE, 1, "game-tom-spinner")});
    Evaluator ev;
    ev.timeline.at(0).commit();

    SECTION("Both prerequisites delivered")
    {
        ev.definitions = DefinitionSet {"tom", 0,
                                        {inSession("game-tom-tenball", {rule(RuleType::Mode, GE, 10, "multiball")}),
                                         unlimited("game-tom-spinner", {rule(RuleType::Event, GE, 10000, "spins")}),
                                         parent}};
        ev.held = {"game-tom-spinner"};
        auto result = ev(parent);
        CHECK_FALSE(result.allSatisfied);
        CHECK(result.rules.at(0) == RuleProgress {0, false});
        CHECK(result.rules.at(1) == RuleProgress {1, true});

        ev.held.insert("game-tom-tenball");
        CHECK(ev(parent).allSatisfied);
    }

    SECTION("A prerequisite not delivered is not judged")
    {
        ev.definitions = DefinitionSet {"tom", 0, {parent}};
        ev.held = {"game-tom-tenball", "game-tom-spinner"};
        const auto result = ev(parent);
        CHECK(result.rules.empty());
        CHECK_FALSE(result.allSatisfied);
    }
}

TEST_CASE("Never happened means zero", "[achievements]")
{
    Evaluator ev;
    ev.timeline.at(0).commit();

    CHECK(ev(inSession("a", {rule(RuleType::Mode, GE, 1, "x")})).rules.at(0) == RuleProgress {0, false});
    CHECK(ev(inSession("b", {rule(RuleType::TimerMode, LE, 60000, "x")})).rules.at(0)
          == RuleProgress {0, true});
    CHECK(ev(inSession("c", {rule(RuleType::Event, Comparison::Ne, 1, "x")})).rules.at(0)
          == RuleProgress {0, true});
}

TEST_CASE("Scores beyond 32 bits", "[achievements]")
{
    const auto def = inSession("game-big", {rule(RuleType::Score, GE, 5'000'000'000)});
    Evaluator ev;
    ev.timeline.at(0).commit();
    ev.timeline.at(1000).score(1, 5'000'000'001).commit();
    CHECK(ev(def).rules.at(0) == RuleProgress {5'000'000'001, true});
}

TEST_CASE("Observation value is the best one for the predicate", "[achievements]")
{
    Evaluator ev;
    ev.timeline.at(0).commit();
    ev.timeline.at(1000).modes({"m"}).commit();
    ev.timeline.at(11000).modes({}).commit();  // 10 s
    ev.timeline.at(12000).modes({"m"}).commit();
    ev.timeline.at(15000).modes({}).commit();  // 3 s

    CHECK(ev(inSession("ge", {rule(RuleType::TimerMode, GE, 60000, "m")})).rules.at(0).value == 10000);
    CHECK(ev(inSession("le", {rule(RuleType::TimerMode, LE, 5000, "m")})).rules.at(0)
          == RuleProgress {3000, true});
    CHECK(ev(inSession("eq", {rule(RuleType::TimerMode, EQ, 10000, "m")})).rules.at(0)
          == RuleProgress {10000, true});
}
