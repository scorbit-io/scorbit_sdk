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

#include <../source/achievement_manager.h>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;

namespace {

const std::string kUser = "4242"; // a user id is a UUID string; any opaque string works in tests

AchievementRule rule(std::string type, std::string comparison, int target,
                     std::string reference = {})
{
    AchievementRule r;
    r.type = std::move(type);
    r.comparison = std::move(comparison);
    r.target = target;
    r.reference = std::move(reference);
    return r;
}

Achievement achievement(std::string key, std::vector<AchievementRule> rules,
                        AchievementInputTime inputTime = AchievementInputTime::Unlimited)
{
    Achievement a;
    a.key = std::move(key);
    a.name = a.key;
    a.rules = std::move(rules);
    a.inputTime = inputTime;
    return a;
}

/** True if `keys` contains `key`. */
bool contains(const std::vector<std::string> &keys, const std::string &key)
{
    for (const auto &k : keys) {
        if (k == key) {
            return true;
        }
    }
    return false;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// P1 - comparison operators are honoured, and ">" is strict
// ------------------------------------------------------------------------------------------------

TEST_CASE("satisfies honours each comparison operator", "[achievements]")
{
    SECTION("greater-than is strict, not >=")
    {
        CHECK_FALSE(satisfies(99, ">", 100));
        CHECK_FALSE(satisfies(100, ">", 100)); // the P1 regression: must NOT pass at the target
        CHECK(satisfies(101, ">", 100));
    }

    SECTION("less-than")
    {
        CHECK(satisfies(99, "<", 100));
        CHECK_FALSE(satisfies(100, "<", 100));
        CHECK_FALSE(satisfies(101, "<", 100));
    }

    SECTION("equals")
    {
        CHECK_FALSE(satisfies(99, "=", 100));
        CHECK(satisfies(100, "=", 100));
        CHECK_FALSE(satisfies(101, "=", 100));
    }

    SECTION("unknown and empty comparisons default to strict greater-than")
    {
        CHECK_FALSE(satisfies(100, "", 100));
        CHECK(satisfies(101, "", 100));
        CHECK_FALSE(satisfies(100, ">=", 100)); // not a platform operator; falls back to ">"
        CHECK(satisfies(101, ">=", 100));
    }
}

TEST_CASE("SCORE rule comparison matrix", "[achievements]")
{
    struct Case {
        const char *comparison;
        int64_t score;
        bool expected;
    };

    // target is 1000 throughout: below / at / above.
    const std::vector<Case> cases {
            {">", 999, false},  {">", 1000, false}, {">", 1001, true},
            {"<", 999, true},   {"<", 1000, false}, {"<", 1001, false},
            {"=", 999, false},  {"=", 1000, true},  {"=", 1001, false},
    };

    for (const auto &c : cases) {
        AchievementManager mgr;
        mgr.setAchievements({achievement("score-ach", {rule("SCORE", c.comparison, 1000, "score")})});

        const auto matched = mgr.checkScoreAchievements(c.score, kUser);
        INFO("comparison=" << c.comparison << " score=" << c.score);
        CHECK(contains(matched, "score-ach") == c.expected);
    }
}

TEST_CASE("MODE rule comparison matrix", "[achievements]")
{
    // A mode event is one occurrence, so the live value the rule sees is 1.
    struct Case {
        const char *comparison;
        int target;
        bool expected;
    };

    const std::vector<Case> cases {
            {">", 0, true},   // the canonical authoring form
            {">", 1, false},  // cannot confirm a second occurrence from one event
            {"=", 1, true}, {"=", 0, false}, {"<", 2, true}, {"<", 1, false},
    };

    for (const auto &c : cases) {
        AchievementManager mgr;
        mgr.setAchievements(
                {achievement("mode-ach", {rule("MODE", c.comparison, c.target, "Grand Finale")})});

        const auto matched = mgr.checkModeAchievements("Grand Finale", "complete", kUser);
        INFO("comparison=" << c.comparison << " target=" << c.target);
        CHECK(contains(matched, "mode-ach") == c.expected);
    }
}

TEST_CASE("Mode rule types require their matching mode event", "[achievements]")
{
    AchievementManager mgr;
    mgr.setAchievements({
            achievement("on-complete", {rule("MODE", ">", 0, "Multiball")}),
            achievement("on-start", {rule("MODE_START", ">", 0, "Multiball")}),
            achievement("on-stack", {rule("MODE_STACK", ">", 0, "Multiball")}),
    });

    SECTION("complete")
    {
        const auto matched = mgr.checkModeAchievements("Multiball", "complete", kUser);
        CHECK(matched == std::vector<std::string> {"on-complete"});
    }

    SECTION("start")
    {
        const auto matched = mgr.checkModeAchievements("Multiball", "start", kUser);
        CHECK(matched == std::vector<std::string> {"on-start"});
    }

    SECTION("stack")
    {
        const auto matched = mgr.checkModeAchievements("Multiball", "stack", kUser);
        CHECK(matched == std::vector<std::string> {"on-stack"});
    }

    SECTION("a different mode name never matches")
    {
        CHECK(mgr.checkModeAchievements("Some Other Mode", "complete", kUser).empty());
    }
}

// ------------------------------------------------------------------------------------------------
// P5 - the counter threshold comes from the PROGRESS rule's target, never from the achievement
// ------------------------------------------------------------------------------------------------

TEST_CASE("incrementProgress unlocks at the PROGRESS rule target", "[achievements]")
{
    AchievementManager mgr;
    // "Shoot 3 ramps". ballCount is deliberately set to a value that would unlock immediately if it
    // were (wrongly) treated as the threshold.
    auto ach = achievement("ramps", {rule("PROGRESS", ">", 3, "ramp_shots")});
    ach.ballCount = 1;
    mgr.setAchievements({ach});

    CHECK_FALSE(mgr.incrementProgress("ramps", kUser, 1, "ramp_shots")); // 1
    CHECK_FALSE(mgr.incrementProgress("ramps", kUser, 1, "ramp_shots")); // 2
    CHECK_FALSE(mgr.incrementProgress("ramps", kUser, 1, "ramp_shots")); // 3, at target, "> 3" fails
    CHECK(mgr.incrementProgress("ramps", kUser, 1, "ramp_shots"));       // 4, crosses

    const auto prog = mgr.getProgress(kUser, "ramps");
    REQUIRE(prog.has_value());
    CHECK(prog->progress == 4);
    CHECK(prog->unlocked);

    SECTION("no further progress accumulates once unlocked (non-trophy)")
    {
        CHECK_FALSE(mgr.incrementProgress("ramps", kUser, 1, "ramp_shots"));
        CHECK(mgr.getProgress(kUser, "ramps")->progress == 4);
    }
}

TEST_CASE("incrementProgress honours the PROGRESS rule comparison", "[achievements]")
{
    SECTION("exact equality unlocks only at the target")
    {
        AchievementManager mgr;
        mgr.setAchievements({achievement("exactly-two", {rule("PROGRESS", "=", 2, "hits")})});

        CHECK_FALSE(mgr.incrementProgress("exactly-two", kUser, 1, "hits"));
        CHECK(mgr.incrementProgress("exactly-two", kUser, 1, "hits")); // == 2
    }

    SECTION("a large increment crossing the target unlocks in one call")
    {
        AchievementManager mgr;
        mgr.setAchievements({achievement("ramps", {rule("PROGRESS", ">", 3, "ramp_shots")})});
        CHECK(mgr.incrementProgress("ramps", kUser, 10, "ramp_shots"));
        CHECK(mgr.getProgress(kUser, "ramps")->progress == 10);
    }
}

TEST_CASE("incrementProgress never claims an unlock without a PROGRESS rule", "[achievements]")
{
    SECTION("achievement has no PROGRESS rule at all")
    {
        AchievementManager mgr;
        auto ach = achievement("mode-only", {rule("MODE", ">", 0, "Multiball")});
        ach.ballCount = 1; // must not be mistaken for a counter threshold
        mgr.setAchievements({ach});

        for (int i = 0; i < 5; ++i) {
            CHECK_FALSE(mgr.incrementProgress("mode-only", kUser, 1));
        }
        // Progress is still recorded; the server remains the arbiter.
        const auto prog = mgr.getProgress(kUser, "mode-only");
        REQUIRE(prog.has_value());
        CHECK(prog->progress == 5);
        CHECK_FALSE(prog->unlocked);
    }

    SECTION("metric key matches no PROGRESS rule")
    {
        AchievementManager mgr;
        mgr.setAchievements({achievement("ramps", {rule("PROGRESS", ">", 1, "ramp_shots")})});

        CHECK_FALSE(mgr.incrementProgress("ramps", kUser, 5, "some_other_metric"));
        CHECK(mgr.getProgress(kUser, "ramps")->progress == 5);
        CHECK_FALSE(mgr.getProgress(kUser, "ramps")->unlocked);
    }

    SECTION("several PROGRESS rules and no metric key is ambiguous")
    {
        AchievementManager mgr;
        mgr.setAchievements({achievement("two-counters",
                                         {rule("PROGRESS", ">", 1, "ramps"),
                                          rule("PROGRESS", ">", 1, "spinners")})});

        CHECK_FALSE(mgr.incrementProgress("two-counters", kUser, 5));
        CHECK_FALSE(mgr.getProgress(kUser, "two-counters")->unlocked);

        // ...but naming the metric resolves it.
        CHECK(mgr.incrementProgress("two-counters", kUser, 1, "ramps"));
    }

    SECTION("unknown achievement key is rejected outright")
    {
        AchievementManager mgr;
        mgr.setAchievements({achievement("known", {rule("PROGRESS", ">", 1, "m")})});
        CHECK_FALSE(mgr.incrementProgress("nope", kUser, 1, "m"));
        CHECK_FALSE(mgr.getProgress(kUser, "nope").has_value());
    }
}

TEST_CASE("A trophy keeps accumulating progress after unlocking", "[achievements]")
{
    AchievementManager mgr;
    auto ach = achievement("trophy", {rule("PROGRESS", ">", 1, "hits")});
    ach.isTrophy = true;
    mgr.setAchievements({ach});

    CHECK_FALSE(mgr.incrementProgress("trophy", kUser, 1, "hits"));
    CHECK(mgr.incrementProgress("trophy", kUser, 1, "hits")); // unlocks at 2

    // Already unlocked, so no *new* unlock, but a trophy can be lost and re-won so it keeps counting.
    CHECK_FALSE(mgr.incrementProgress("trophy", kUser, 1, "hits"));
    CHECK(mgr.getProgress(kUser, "trophy")->progress == 3);
}

// ------------------------------------------------------------------------------------------------
// P3 - conservative matching for mixed mode+score achievements
// ------------------------------------------------------------------------------------------------

TEST_CASE("A mixed mode+score achievement matches only when both are judged", "[achievements]")
{
    AchievementManager mgr;
    mgr.setAchievements({achievement("finale-billionaire",
                                     {rule("MODE", ">", 0, "Grand Finale"),
                                      rule("SCORE", ">", 1000, "score")})});

    SECTION("score-only path withholds even when the score qualifies")
    {
        // The P3b regression: the mode rule is unevaluable here and must not be treated as passing.
        CHECK(mgr.checkScoreAchievements(5000, kUser).empty());
    }

    SECTION("mode-only path withholds because no score was supplied")
    {
        // The P3a regression: the score rule is unevaluable here, not compared against a fabricated 0.
        CHECK(mgr.checkModeAchievements("Grand Finale", "complete", kUser).empty());
    }

    SECTION("the combined path matches when both rules are satisfied")
    {
        const auto matched =
                mgr.checkModeAchievementsWithScore("Grand Finale", "complete", kUser, 5000);
        CHECK(matched == std::vector<std::string> {"finale-billionaire"});
    }

    SECTION("the combined path withholds when the score falls short")
    {
        CHECK(mgr.checkModeAchievementsWithScore("Grand Finale", "complete", kUser, 500).empty());
    }

    SECTION("the combined path withholds when the mode does not match")
    {
        CHECK(mgr.checkModeAchievementsWithScore("Other Mode", "complete", kUser, 5000).empty());
    }
}

TEST_CASE("Two mode rules cannot both be judged from a single mode event", "[achievements]")
{
    // "Complete mode A and mode B" is two MODE rules. One event cannot satisfy both, so neither
    // entry point reports it - the server decides.
    AchievementManager mgr;
    mgr.setAchievements({achievement(
            "both-modes", {rule("MODE", ">", 0, "Mode A"), rule("MODE", ">", 0, "Mode B")})});

    CHECK(mgr.checkModeAchievements("Mode A", "complete", kUser).empty());
    CHECK(mgr.checkModeAchievements("Mode B", "complete", kUser).empty());
}

TEST_CASE("Rule types the SDK cannot judge withhold the match", "[achievements]")
{
    AchievementManager mgr;
    mgr.setAchievements({
            achievement("game-code", {rule("SCORE", ">", 100, "score"), rule("GAME_CODE", ">", 0)}),
            achievement("timer", {rule("SCORE", ">", 100, "score"), rule("TIMER", "<", 60000)}),
            achievement("event", {rule("SCORE", ">", 100, "score"), rule("EVENT", ">", 0)}),
            achievement("attempt", {rule("SCORE", ">", 100, "score"), rule("ATTEMPT", ">", 0)}),
            achievement("unknown", {rule("SCORE", ">", 100, "score"), rule("WHAT_IS_THIS", ">", 0)}),
            achievement("plain-score", {rule("SCORE", ">", 100, "score")}),
    });

    // Only the achievement with nothing unjudgeable is reported; a GAME_CODE achievement in
    // particular must never auto-unlock.
    const auto matched = mgr.checkScoreAchievements(5000, kUser);
    CHECK(matched == std::vector<std::string> {"plain-score"});
}

// ------------------------------------------------------------------------------------------------
// Skip semantics - ACHIEVEMENT/PROGRESS are skipped, not failed
// ------------------------------------------------------------------------------------------------

TEST_CASE("ACHIEVEMENT and PROGRESS rules are skipped, not failed", "[achievements]")
{
    AchievementManager mgr;
    mgr.setAchievements({
            // Server-owned rules sit alongside a locally evaluable one; they must not block it.
            achievement("score-plus-chain",
                        {rule("SCORE", ">", 100, "score"), rule("ACHIEVEMENT", ">", 0),
                         rule("PROGRESS", ">", 999, "ramps")}),
            achievement("mode-plus-chain",
                        {rule("MODE", ">", 0, "Multiball"), rule("ACHIEVEMENT", ">", 0),
                         rule("PROGRESS", ">", 999, "ramps")}),
    });

    SECTION("score path")
    {
        const auto matched = mgr.checkScoreAchievements(5000, kUser);
        CHECK(matched == std::vector<std::string> {"score-plus-chain"});
    }

    SECTION("mode path")
    {
        const auto matched = mgr.checkModeAchievements("Multiball", "complete", kUser);
        CHECK(matched == std::vector<std::string> {"mode-plus-chain"});
    }
}

TEST_CASE("A global-scoped achievement is never matched locally, regardless of its rules",
          "[achievements]")
{
    // Routing is normally derived purely from rule types (MODE/SCORE => local, ACHIEVEMENT/
    // PROGRESS => server). scope == "global" is the one exception: always server-evaluated,
    // even when every rule present is otherwise locally evaluable.
    AchievementManager mgr;

    auto globalScore = achievement("global-score", {rule("SCORE", ">", 100, "score")});
    globalScore.scope = "global";

    auto globalMode = achievement("global-mode", {rule("MODE", ">", 0, "Multiball")});
    globalMode.scope = "global";

    auto gameScore = achievement("game-score", {rule("SCORE", ">", 100, "score")});
    gameScore.scope = "game";

    mgr.setAchievements({globalScore, globalMode, gameScore});

    SECTION("score path ignores the global achievement but still matches the game-scoped one")
    {
        const auto matched = mgr.checkScoreAchievements(5000, kUser);
        CHECK(matched == std::vector<std::string> {"game-score"});
    }

    SECTION("mode path ignores the global achievement")
    {
        CHECK(mgr.checkModeAchievements("Multiball", "complete", kUser).empty());
    }

    SECTION("mode+score path ignores the global achievement but still matches the game-scoped one")
    {
        const auto matched = mgr.checkModeAchievementsWithScore("Multiball", "complete", kUser, 5000);
        CHECK(matched == std::vector<std::string> {"game-score"});
    }
}

TEST_CASE("An achievement whose rules are all skipped never matches", "[achievements]")
{
    AchievementManager mgr;
    mgr.setAchievements({
            achievement("all-server-owned",
                        {rule("ACHIEVEMENT", ">", 0), rule("PROGRESS", ">", 1, "ramps")}),
            achievement("no-rules", {}),
    });

    CHECK(mgr.checkScoreAchievements(999999, kUser).empty());
    CHECK(mgr.checkModeAchievements("Multiball", "complete", kUser).empty());
    CHECK(mgr.checkModeAchievementsWithScore("Multiball", "complete", kUser, 999999).empty());
}

TEST_CASE("The score path only reports achievements carrying a SCORE rule", "[achievements]")
{
    AchievementManager mgr;
    mgr.setAchievements({achievement("mode-only", {rule("MODE", ">", 0, "Multiball")})});
    CHECK(mgr.checkScoreAchievements(999999, kUser).empty());
}

// ------------------------------------------------------------------------------------------------
// isAlreadyUnlocked - Limited re-checks, Unlimited skips
// ------------------------------------------------------------------------------------------------

TEST_CASE("isAlreadyUnlocked skips Unlimited but re-checks Limited", "[achievements]")
{
    AchievementManager mgr;
    mgr.setAchievements({
            achievement("lifetime", {rule("SCORE", ">", 100, "score")},
                        AchievementInputTime::Unlimited),
            achievement("session", {rule("SCORE", ">", 100, "score")},
                        AchievementInputTime::Limited),
    });

    SECTION("both match while un-unlocked")
    {
        const auto matched = mgr.checkScoreAchievements(5000, kUser);
        CHECK(contains(matched, "lifetime"));
        CHECK(contains(matched, "session"));
    }

    SECTION("once unlocked, only the session-scoped one is re-checked")
    {
        mgr.updateProgress(kUser, "lifetime", 1, true);
        mgr.updateProgress(kUser, "session", 1, true);

        const auto matched = mgr.checkScoreAchievements(5000, kUser);
        CHECK_FALSE(contains(matched, "lifetime"));
        CHECK(contains(matched, "session"));
    }

    SECTION("progress recorded but not unlocked does not skip anything")
    {
        mgr.updateProgress(kUser, "lifetime", 1, false);
        CHECK(contains(mgr.checkScoreAchievements(5000, kUser), "lifetime"));
    }

    SECTION("another user's unlock does not affect this one")
    {
        mgr.updateProgress("999", "lifetime", 1, true);
        CHECK(contains(mgr.checkScoreAchievements(5000, kUser), "lifetime"));
    }
}

// ------------------------------------------------------------------------------------------------
// P4 - the triggered callback runs with no manager mutex held
// ------------------------------------------------------------------------------------------------

TEST_CASE("A re-entrant triggered callback does not deadlock", "[achievements]")
{
    AchievementManager mgr;
    mgr.setAchievements({achievement("ramps", {rule("PROGRESS", ">", 1, "ramp_shots")})});

    int calls = 0;
    std::optional<AchievementProgress> seen;
    bool sawDefinition = false;
    std::vector<std::string> sawMatches;

    // Every one of these reaches back into the manager. If any manager mutex were held while the
    // callback ran, this would hang rather than fail.
    mgr.setTriggeredCallback(
            [&](const std::string &key, const std::string &userId, bool /*isUnlock*/, int /*progress*/) {
                ++calls;
                seen = mgr.getProgress(userId, key);
                sawDefinition = mgr.getAchievement(key).has_value();
                sawMatches = mgr.checkScoreAchievements(1, userId);
                mgr.getUserProgress(userId);
                mgr.getAchievements();
                mgr.hasAchievements();
                mgr.hasDmdFrame(key);
            });

    CHECK_FALSE(mgr.incrementProgress("ramps", kUser, 1, "ramp_shots"));
    REQUIRE(calls == 1);
    REQUIRE(seen.has_value());
    CHECK(seen->progress == 1);
    CHECK_FALSE(seen->unlocked);
    CHECK(sawDefinition);
    // A PROGRESS-only achievement is never a score match, so the re-entrant check returns nothing -
    // what matters is that it returned at all rather than deadlocking.
    CHECK(sawMatches.empty());

    CHECK(mgr.incrementProgress("ramps", kUser, 1, "ramp_shots"));
    REQUIRE(calls == 2);
    REQUIRE(seen.has_value());
    // The callback observes the already-committed state, including the unlock.
    CHECK(seen->progress == 2);
    CHECK(seen->unlocked);
}

TEST_CASE("The triggered callback reports unlock state and progress", "[achievements]")
{
    AchievementManager mgr;
    mgr.setAchievements({achievement("ramps", {rule("PROGRESS", ">", 2, "ramp_shots")})});

    std::vector<std::pair<bool, int>> events;
    std::string lastKey;
    std::string lastUser;
    mgr.setTriggeredCallback(
            [&](const std::string &key, const std::string &userId, bool isUnlock, int progress) {
                lastKey = key;
                lastUser = userId;
                events.emplace_back(isUnlock, progress);
            });

    mgr.incrementProgress("ramps", kUser, 1, "ramp_shots");
    mgr.incrementProgress("ramps", kUser, 1, "ramp_shots");
    mgr.incrementProgress("ramps", kUser, 1, "ramp_shots");

    CHECK(lastKey == "ramps");
    CHECK(lastUser == kUser);
    REQUIRE(events.size() == 3);
    CHECK(events[0] == std::pair<bool, int> {false, 1});
    CHECK(events[1] == std::pair<bool, int> {false, 2});
    CHECK(events[2] == std::pair<bool, int> {true, 3}); // "> 2" crosses at 3

    SECTION("a re-entrant callback that increments a different achievement is safe")
    {
        AchievementManager other;
        other.setAchievements({achievement("a", {rule("PROGRESS", ">", 0, "m")}),
                               achievement("b", {rule("PROGRESS", ">", 0, "m")})});
        int nested = 0;
        other.setTriggeredCallback([&](const std::string &key, const std::string &userId, bool, int) {
            if (key == "a" && nested == 0) {
                ++nested;
                other.incrementProgress("b", userId, 1, "m");
            }
        });
        CHECK(other.incrementProgress("a", kUser, 1, "m"));
        CHECK(other.getProgress(kUser, "b")->unlocked);
    }
}

TEST_CASE("Clearing the triggered callback stops notifications", "[achievements]")
{
    AchievementManager mgr;
    mgr.setAchievements({achievement("ramps", {rule("PROGRESS", ">", 5, "ramp_shots")})});

    int calls = 0;
    mgr.setTriggeredCallback([&](const std::string &, const std::string &, bool, int) { ++calls; });
    mgr.incrementProgress("ramps", kUser, 1, "ramp_shots");
    CHECK(calls == 1);

    mgr.setTriggeredCallback(nullptr);
    mgr.incrementProgress("ramps", kUser, 1, "ramp_shots");
    CHECK(calls == 1);
}

// ------------------------------------------------------------------------------------------------
// K4 - session-scoped progress reset
// ------------------------------------------------------------------------------------------------

TEST_CASE("clearUserProgress resets one user only", "[achievements]")
{
    const std::string kOther = "777";

    AchievementManager mgr;
    mgr.setAchievements({achievement("ramps", {rule("PROGRESS", ">", 3, "ramp_shots")},
                                     AchievementInputTime::Limited)});

    mgr.incrementProgress("ramps", kUser, 2, "ramp_shots");
    mgr.incrementProgress("ramps", kOther, 2, "ramp_shots");
    REQUIRE(mgr.getProgress(kUser, "ramps")->progress == 2);
    REQUIRE(mgr.getProgress(kOther, "ramps")->progress == 2);

    mgr.clearUserProgress(kUser);

    CHECK_FALSE(mgr.getProgress(kUser, "ramps").has_value());
    CHECK_FALSE(mgr.getUserProgress(kUser).has_value());
    CHECK(mgr.getProgress(kOther, "ramps")->progress == 2);

    SECTION("a session-scoped counter starts over rather than unlocking early")
    {
        // Without the reset the two carried-over shots plus two more would cross "> 3".
        CHECK_FALSE(mgr.incrementProgress("ramps", kUser, 2, "ramp_shots"));
        CHECK(mgr.getProgress(kUser, "ramps")->progress == 2);
        // The other user, un-reset, does cross.
        CHECK(mgr.incrementProgress("ramps", kOther, 2, "ramp_shots"));
    }

    SECTION("clearing an unknown user is harmless")
    {
        mgr.clearUserProgress("123456");
        CHECK(mgr.getProgress(kOther, "ramps")->progress == 2);
    }
}

TEST_CASE("clearAllProgress resets every user and leaves definitions intact", "[achievements]")
{
    const std::string kOther = "777";

    AchievementManager mgr;
    mgr.setAchievements({achievement("ramps", {rule("PROGRESS", ">", 3, "ramp_shots")},
                                     AchievementInputTime::Limited)});

    mgr.incrementProgress("ramps", kUser, 2, "ramp_shots");
    mgr.incrementProgress("ramps", kOther, 2, "ramp_shots");

    mgr.clearAllProgress();

    CHECK_FALSE(mgr.getUserProgress(kUser).has_value());
    CHECK_FALSE(mgr.getUserProgress(kOther).has_value());
    CHECK_FALSE(mgr.getProgress(kUser, "ramps").has_value());

    // Definitions survive - only progress was cleared.
    CHECK(mgr.hasAchievements());
    CHECK(mgr.getAchievement("ramps").has_value());
}

TEST_CASE("clearAllProgress re-enables an unlocked Unlimited achievement locally", "[achievements]")
{
    AchievementManager mgr;
    mgr.setAchievements({achievement("lifetime", {rule("SCORE", ">", 100, "score")},
                                     AchievementInputTime::Unlimited)});

    mgr.updateProgress(kUser, "lifetime", 1, true);
    REQUIRE(mgr.checkScoreAchievements(5000, kUser).empty());

    mgr.clearAllProgress();
    CHECK(contains(mgr.checkScoreAchievements(5000, kUser), "lifetime"));
}

// ------------------------------------------------------------------------------------------------
// Caches
// ------------------------------------------------------------------------------------------------

TEST_CASE("Definition cache basics", "[achievements]")
{
    AchievementManager mgr;
    CHECK_FALSE(mgr.hasAchievements());
    CHECK(mgr.getAchievements().empty());
    CHECK_FALSE(mgr.getAchievement("nope").has_value());

    mgr.setAchievements({achievement("a", {rule("SCORE", ">", 1, "score")}),
                         achievement("b", {rule("SCORE", ">", 2, "score")})});

    CHECK(mgr.hasAchievements());
    CHECK(mgr.getAchievements().size() == 2);
    REQUIRE(mgr.getAchievement("b").has_value());
    CHECK(mgr.getAchievement("b")->rules.at(0).target == 2);

    SECTION("setAchievements replaces wholesale, dropping stale keys")
    {
        mgr.setAchievements({achievement("c", {rule("SCORE", ">", 3, "score")})});
        CHECK(mgr.getAchievements().size() == 1);
        CHECK_FALSE(mgr.getAchievement("a").has_value());
        CHECK(mgr.getAchievement("c").has_value());
    }

    SECTION("clearAchievements empties the cache")
    {
        mgr.clearAchievements();
        CHECK_FALSE(mgr.hasAchievements());
        CHECK_FALSE(mgr.getAchievement("a").has_value());
    }
}

TEST_CASE("Progress cache basics", "[achievements]")
{
    AchievementManager mgr;
    CHECK_FALSE(mgr.getUserProgress(kUser).has_value());

    AchievementProgress p1;
    p1.key = "a";
    p1.progress = 3;
    p1.unlocked = false;
    AchievementProgress p2;
    p2.key = "b";
    p2.progress = 7;
    p2.unlocked = true;
    p2.unlockedAt = "2026-08-08T00:00:00Z";

    mgr.setUserProgress(kUser, {p1, p2});

    const auto all = mgr.getUserProgress(kUser);
    REQUIRE(all.has_value());
    CHECK(all->size() == 2);

    REQUIRE(mgr.getProgress(kUser, "b").has_value());
    CHECK(mgr.getProgress(kUser, "b")->progress == 7);
    CHECK(mgr.getProgress(kUser, "b")->unlocked);
    CHECK(mgr.getProgress(kUser, "b")->unlockedAt == "2026-08-08T00:00:00Z");
    CHECK_FALSE(mgr.getProgress(kUser, "missing").has_value());

    SECTION("setUserProgress replaces wholesale")
    {
        AchievementProgress p3;
        p3.key = "c";
        p3.progress = 1;
        mgr.setUserProgress(kUser, {p3});
        CHECK(mgr.getUserProgress(kUser)->size() == 1);
        CHECK_FALSE(mgr.getProgress(kUser, "a").has_value());
    }

    SECTION("updateProgress overwrites an entry outright")
    {
        mgr.updateProgress(kUser, "a", 42, true);
        CHECK(mgr.getProgress(kUser, "a")->progress == 42);
        CHECK(mgr.getProgress(kUser, "a")->unlocked);
    }
}

TEST_CASE("DMD frame cache", "[achievements]")
{
    AchievementManager mgr;
    CHECK_FALSE(mgr.hasDmdFrame("a"));
    CHECK(mgr.getDmdFrame("a").empty());

    mgr.setDmdFrame("a", DmdFrame {1, 2, 3});
    CHECK(mgr.hasDmdFrame("a"));
    CHECK(mgr.getDmdFrame("a") == DmdFrame {1, 2, 3});

    SECTION("frames beyond capacity evict the least recently used")
    {
        for (int i = 0; i < MAX_DMD_FRAMES_CACHED; ++i) {
            mgr.setDmdFrame("k" + std::to_string(i), DmdFrame {static_cast<uint8_t>(i)});
        }
        // "a" was the oldest entry and has been pushed out.
        CHECK_FALSE(mgr.hasDmdFrame("a"));
        CHECK(mgr.hasDmdFrame("k" + std::to_string(MAX_DMD_FRAMES_CACHED - 1)));
    }

    SECTION("getFramesToDownload lists definitions with an image but no cached frame")
    {
        auto withImage = achievement("with-image", {rule("SCORE", ">", 1, "score")});
        withImage.imageUrl = "https://example.test/a.png";
        auto cached = achievement("cached", {rule("SCORE", ">", 1, "score")});
        cached.imageUrl = "https://example.test/b.png";
        auto noImage = achievement("no-image", {rule("SCORE", ">", 1, "score")});

        mgr.setAchievements({withImage, cached, noImage});
        mgr.setDmdFrame("cached", DmdFrame {9});

        const auto todo = mgr.getFramesToDownload();
        REQUIRE(todo.size() == 1);
        CHECK(todo.at(0).first == "with-image");
        CHECK(todo.at(0).second == "https://example.test/a.png");
    }
}
