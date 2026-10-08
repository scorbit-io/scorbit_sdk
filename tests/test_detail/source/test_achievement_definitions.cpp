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

#include <../source/achievements/json_codec.h>
#include <../source/achievements/definition_check.h>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit::detail::achievements;
using json = nlohmann::json;

namespace {

json rule(const char *type, const char *comparison, int64_t target, const char *reference = "")
{
    return {{"type", type},
            {"comparison", comparison},
            {"target", target},
            {"reference", reference}};
}

constexpr bool IN_SESSION = true;
constexpr bool UNLIMITED = false;

// nlohmann turns a one-element brace list into that element, so arrays are built explicitly
json achievement(const char *key, std::vector<json> rules, bool singleSession = IN_SESSION)
{
    return {{"key", key},
            {"name", key},
            {"scope", "game"},
            {"is_single_session", singleSession},
            {"rules", json(std::move(rules))}};
}

DefinitionSet parse(std::vector<json> results)
{
    auto set = parseDefinitionsResponse(json(std::move(results)));
    REQUIRE(set);
    return *set;
}

} // namespace

TEST_CASE("Predicates are inclusive", "[achievements]")
{
    CHECK(satisfies(5, Comparison::Ge, 5));
    CHECK_FALSE(satisfies(4, Comparison::Ge, 5));
    CHECK(satisfies(5, Comparison::Le, 5));
    CHECK_FALSE(satisfies(6, Comparison::Le, 5));
    CHECK(satisfies(5, Comparison::Eq, 5));
    CHECK_FALSE(satisfies(5, Comparison::Ne, 5));
    CHECK(satisfies(4, Comparison::Ne, 5));
}

TEST_CASE("Parse a full definitions response", "[achievements]")
{
    // As the API's ScorbitronAchievementSerializer renders it
    json prerequisite = rule("ACHIEVEMENT", ">=", 1, "game-cv-boom-balloon");
    prerequisite["subachievement"] = 1207;
    const json document = json::array({
            {{"id", "0b7c4f0e-5c1e-4f55-9a43-0f6d2b8f6a11"},
             {"key", "game-cv-boom-balloon"},
             {"name", "Boom Balloon"},
             {"description", "Pop the balloon during multiball."},
             {"icon", "https://cdn.example/ach/boom-balloon.png"},
             {"is_single_session", true},
             {"is_trophy", false},
             {"is_badge", true},
             {"scope", "game"},
             {"visible", true},
             {"obscure", false},
             {"obscure_image", nullptr},
             {"group_id", 42},
             {"level", 1000},
             {"display_position", 1},
             {"frame", "https://cdn.example/achievement_frame/game-cv-boom-balloon_3.png"},
             {"frame_version", 4},
             {"notify_when_achieved", true},
             {"rules", json::array({{{"type", "MODE"},
                                     {"comparison", ">="},
                                     {"target", 1},
                                     {"reference", "balloon"},
                                     {"subachievement", nullptr}}})}},
            {{"key", "game-cv-balloon-fan"},
             {"scope", "game"},
             {"is_single_session", false},
             {"frame", nullptr},
             {"frame_version", 0},
             {"rules", json::array({prerequisite})}},
    });

    const auto set = parseDefinitionsResponse(document);
    REQUIRE(set);
    REQUIRE(set->size() == 2);

    const auto *def = set->find("game-cv-boom-balloon");
    REQUIRE(def);
    CHECK(def->name == "Boom Balloon");
    CHECK(def->scope == Scope::Game);
    CHECK(def->evaluation == EvaluationClass::InSession);
    CHECK(def->visible);
    CHECK_FALSE(def->isTrophy);
    CHECK(def->isBadge);
    CHECK(def->iconUrl == "https://cdn.example/ach/boom-balloon.png");
    CHECK(def->obscureImageUrl.empty());
    CHECK(def->groupId == 42);
    CHECK(def->level == 1000);
    CHECK(def->displayPosition == 1);
    CHECK(def->frameUrl == "https://cdn.example/achievement_frame/game-cv-boom-balloon_3.png");
    CHECK(def->frameVersion == 4);
    CHECK(def->notifyWhenAchieved);
    REQUIRE(def->rules.size() == 1);
    CHECK(def->rules[0].type == RuleType::Mode);
    CHECK(def->rules[0].comparison == Comparison::Ge);
    CHECK(def->rules[0].target == 1);
    CHECK(def->rules[0].reference == "balloon");

    const auto *fan = set->find("game-cv-balloon-fan");
    REQUIRE(fan);
    CHECK(fan->evaluation == EvaluationClass::Unlimited);
    CHECK(fan->frameUrl.empty());
    CHECK(fan->rules[0].type == RuleType::Achievement);
    CHECK(fan->rules[0].reference == "game-cv-boom-balloon"); // the key, not the pk
}

TEST_CASE("Comparisons are the API's symbols", "[achievements]")
{
    const auto set =
            parse({achievement("game-cmp", {rule("SCORE", "=", 1), rule("SCORE", "<=", 2),
                                            rule("SCORE", ">=", 3), rule("SCORE", "!=", 4)})});
    REQUIRE(set.size() == 1);
    const auto &rules = set.all().front().rules;
    CHECK(rules[0].comparison == Comparison::Eq);
    CHECK(rules[1].comparison == Comparison::Le);
    CHECK(rules[2].comparison == Comparison::Ge);
    CHECK(rules[3].comparison == Comparison::Ne);
    CHECK(std::string {toString(Comparison::Ge)} == ">=");
}

TEST_CASE("Null optional fields read as absent", "[achievements]")
{
    json def = achievement("game-x", {rule("SCORE", ">=", 1)});
    def["group_id"] = nullptr;
    def["level"] = nullptr;
    def["visible"] = nullptr;
    def["icon"] = nullptr;
    def["frame"] = nullptr;
    def["rules"][0]["reference"] = nullptr;

    const auto set = parse({def});
    REQUIRE(set.size() == 1);
    const auto &d = set.all().front();
    CHECK_FALSE(d.groupId);
    CHECK_FALSE(d.level);
    CHECK(d.visible); // default
    CHECK(d.iconUrl.empty());
    CHECK(d.frameUrl.empty());
    CHECK(d.rules[0].reference.empty());
}

TEST_CASE("Targets are int64", "[achievements]")
{
    const auto set = parse(
            {achievement("game-bop-billion", {rule("SCORE", ">=", 5'000'000'000)}, UNLIMITED)});
    REQUIRE(set.size() == 1);
    CHECK(set.all().front().rules[0].target == 5'000'000'000);
    CHECK(set.all().front().evaluation == EvaluationClass::Unlimited);
}

TEST_CASE("MODE_STACK reference is split, trimmed and de-duplicated", "[achievements]")
{
    const auto set = parse({achievement(
            "game-stack", {rule("MODE_STACK", ">=", 3, "multiball, wizard ,multiball")})});
    REQUIRE(set.size() == 1);
    CHECK(set.all().front().rules[0].stackModes
          == std::vector<std::string> {"multiball", "wizard"});
}

TEST_CASE("Only the total score is evaluable", "[achievements]")
{
    const auto set = parse({
            achievement("game-blank", {rule("SCORE", ">=", 1)}),
            achievement("game-total", {rule("SCORE", ">=", 1, "score")}),
            achievement("game-per-ball", {rule("SCORE", ">=", 1, "ball_score")}),
    });
    CHECK(set.contains("game-blank"));
    CHECK(set.contains("game-total"));
    CHECK_FALSE(set.contains("game-per-ball"));
}

TEST_CASE("Malformed and unevaluable definitions are skipped, the rest kept", "[achievements]")
{
    json noEvaluation = achievement("game-no-evaluation", {rule("SCORE", ">=", 1)});
    noEvaluation.erase("is_single_session");
    json oldEvaluation = noEvaluation;
    oldEvaluation["key"] = "game-old-evaluation";
    oldEvaluation["evaluation"] = "in_session";

    const auto set = parse({
            achievement("game-good", {rule("MODE", ">=", 1, "multiball")}),
            json {{"key", "game-no-scope"}, {"is_single_session", true}, {"rules", json::array()}},
            noEvaluation,
            oldEvaluation,
            achievement("game-unknown-type", {rule("PROGRESS", ">=", 1)}),
            achievement("game-strict-gt", {rule("SCORE", ">", 1)}),
            achievement("game-contract-name", {rule("SCORE", "GE", 1)}),
            achievement("game-target-string",
                        {json {{"type", "SCORE"}, {"comparison", ">="}, {"target", "big"}}}),
            achievement("game-no-rules", {}),
            achievement("game-mode-no-ref", {rule("MODE", ">=", 1)}),
            achievement("game-stack-of-one", {rule("MODE_STACK", ">=", 1, "multiball")}),
            achievement("game-lonely-ball", {rule("BALL", "=", 1)}),
            achievement("game-ball-with-score", {rule("SCORE", ">=", 1), rule("BALL", "=", 1)}),
            achievement("game-between-ok",
                        {rule("MODE", ">=", 2, "multiball"), rule("TIMER_BETWEEN", "<=", 30000)}),
            json("not an object"),
    });

    CHECK(set.size() == 2);
    CHECK(set.contains("game-good"));
    CHECK(set.contains("game-between-ok"));
}

TEST_CASE("ACHIEVEMENT rules require the prerequisite to be held", "[achievements]")
{
    const auto set = parse({
            achievement("game-ge-1", {rule("ACHIEVEMENT", ">=", 1, "elsewhere")}, UNLIMITED),
            achievement("game-eq-1", {rule("ACHIEVEMENT", "=", 1, "elsewhere")}, UNLIMITED),
            achievement("game-eq-0", {rule("ACHIEVEMENT", "=", 0, "elsewhere")}, UNLIMITED),
            achievement("game-ge-2", {rule("ACHIEVEMENT", ">=", 2, "elsewhere")}, UNLIMITED),
            achievement("game-le-1", {rule("ACHIEVEMENT", "<=", 1, "elsewhere")}, UNLIMITED),
            achievement("game-ne-1", {rule("ACHIEVEMENT", "!=", 1, "elsewhere")}, UNLIMITED),
    });

    CHECK(set.size() == 2);
    CHECK(set.contains("game-ge-1"));
    CHECK(set.contains("game-eq-1"));
}

TEST_CASE("Achievements depending on an ACHIEVEMENT cycle are skipped", "[achievements]")
{
    const auto set = parse({
            achievement("a", {rule("ACHIEVEMENT", ">=", 1, "b")}, UNLIMITED),
            achievement("b", {rule("ACHIEVEMENT", ">=", 1, "a")}, UNLIMITED),
            achievement("c", {rule("ACHIEVEMENT", ">=", 1, "a")}, UNLIMITED),
            achievement("d", {rule("ACHIEVEMENT", ">=", 1, "e")}, UNLIMITED),
            achievement("e", {rule("MODE", ">=", 1, "multiball")}),
            achievement("f", {rule("ACHIEVEMENT", ">=", 1, "elsewhere")}, UNLIMITED),
    });

    CHECK_FALSE(set.contains("a"));
    CHECK_FALSE(set.contains("b"));
    CHECK_FALSE(set.contains("c"));
    CHECK(set.contains("d"));
    CHECK(set.contains("e"));
    CHECK(set.contains("f")); // prerequisite not delivered: not a local cycle
}

TEST_CASE("Non-definitions documents are rejected", "[achievements]")
{
    CHECK_FALSE(parseDefinitionsResponse(json {{"results", json::array()}}));
    CHECK_FALSE(parseDefinitionsResponse(json("nope")));

    const auto none = parseDefinitionsResponse(json::array());
    REQUIRE(none);
    CHECK(none->empty());
}

TEST_CASE("Parse a sparse progress response", "[achievements]")
{
    // As the API's UserAchievementV2Serializer renders it
    const auto entry = [](const char *key, json achieved, json currentValue) {
        return json {{"id", "6d1f2a90-3c4b-4f1e-8d7a-2b5c9e0f1a33"},
                     {"achievement",
                      {{"id", "0b7c4f0e-5c1e-4f55-9a43-0f6d2b8f6a11"},
                       {"key", key},
                       {"name", key},
                       {"scope", "game"},
                       {"group_id", nullptr},
                       {"game", {{"id", "c1"}, {"name", "Cactus Canyon"}, {"slug", "cv"}}}}},
                     {"current_value", std::move(currentValue)},
                     {"achieved", std::move(achieved)},
                     {"achieved_time", nullptr},
                     {"achieved_venue", nullptr},
                     {"achieved_venue_machine", nullptr},
                     {"progress", 74}};
    };
    json headless = entry("game-cv-nameless", true, 1.0);
    headless.erase("achievement");

    const auto baselines = parseProgressResponse(json::array({
            entry("game-cv-marathon", false, 7400.0),
            entry("game-cv-boom", true, nullptr),
            entry("game-cv-null", nullptr, nullptr),
            headless,
    }));
    REQUIRE(baselines);
    REQUIRE(baselines->size() == 3);

    // current_value is not a per-rule measurement
    CHECK_FALSE(baselines->at("game-cv-marathon").achieved);
    CHECK(baselines->at("game-cv-marathon").rules.empty());
    CHECK(baselines->at("game-cv-boom").achieved);
    CHECK_FALSE(baselines->at("game-cv-null").achieved);
}

TEST_CASE("An empty progress response means nothing started", "[achievements]")
{
    const auto baselines = parseProgressResponse(json::array());
    REQUIRE(baselines);
    CHECK(baselines->empty());

    CHECK_FALSE(parseProgressResponse({{"user_id", "3f1c"}, {"results", json::array()}}));
}
