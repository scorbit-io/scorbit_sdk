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
    return {{"type", type}, {"comparison", comparison}, {"target", target}, {"reference", reference}};
}

// nlohmann turns a one-element brace list into that element, so arrays are built explicitly
json achievement(const char *key, std::vector<json> rules, const char *evaluation = "in_session")
{
    return {{"key", key}, {"name", key}, {"scope", "game"}, {"evaluation", evaluation},
            {"rules", json(std::move(rules))}};
}

DefinitionSet parse(std::vector<json> results)
{
    auto set = parseDefinitionsResponse({{"game", "cactus-canyon"},
                                         {"frames_version", 7},
                                         {"results", json(std::move(results))}});
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
    const json document = {
            {"game", "cactus-canyon"},
            {"frames_version", 7},
            {"results",
             {{{"key", "game-cv-boom-balloon"},
               {"name", "Boom Balloon"},
               {"description", "Pop the balloon during multiball."},
               {"scope", "game"},
               {"game", "cactus-canyon"},
               {"venue", nullptr},
               {"evaluation", "in_session"},
               {"is_trophy", false},
               {"visible", true},
               {"obscure", false},
               {"icon_url", "https://cdn.example/ach/boom-balloon.png"},
               {"obscure_image_url", nullptr},
               {"group_id", 42},
               {"level", 1000},
               {"display_position", 1},
               {"notify_when_achieved", true},
               {"rules", json::array({rule("MODE", "GE", 1, "balloon")})}}}},
    };

    const auto set = parseDefinitionsResponse(document);
    REQUIRE(set);
    CHECK(set->game() == "cactus-canyon");
    CHECK(set->framesVersion() == 7);
    REQUIRE(set->size() == 1);

    const auto *def = set->find("game-cv-boom-balloon");
    REQUIRE(def);
    CHECK(def->name == "Boom Balloon");
    CHECK(def->scope == Scope::Game);
    CHECK(def->evaluation == EvaluationClass::InSession);
    CHECK(def->visible);
    CHECK(def->iconUrl == "https://cdn.example/ach/boom-balloon.png");
    CHECK(def->obscureImageUrl.empty());
    CHECK(def->groupId == 42);
    CHECK(def->level == 1000);
    CHECK(def->displayPosition == 1);
    CHECK(def->notifyWhenAchieved);
    REQUIRE(def->rules.size() == 1);
    CHECK(def->rules[0].type == RuleType::Mode);
    CHECK(def->rules[0].comparison == Comparison::Ge);
    CHECK(def->rules[0].target == 1);
    CHECK(def->rules[0].reference == "balloon");
}

TEST_CASE("Null optional fields read as absent", "[achievements]")
{
    json def = achievement("game-x", {rule("SCORE", "GE", 1)});
    def["group_id"] = nullptr;
    def["level"] = nullptr;
    def["visible"] = nullptr;
    def["icon_url"] = nullptr;
    def["rules"][0]["reference"] = nullptr;

    const auto set = parse({def});
    REQUIRE(set.size() == 1);
    const auto &d = set.all().front();
    CHECK_FALSE(d.groupId);
    CHECK_FALSE(d.level);
    CHECK(d.visible); // default
    CHECK(d.rules[0].reference.empty());
}

TEST_CASE("Targets are int64", "[achievements]")
{
    const auto set = parse({achievement("game-bop-billion", {rule("SCORE", "GE", 5'000'000'000)},
                                        "unlimited")});
    REQUIRE(set.size() == 1);
    CHECK(set.all().front().rules[0].target == 5'000'000'000);
    CHECK(set.all().front().evaluation == EvaluationClass::Unlimited);
}

TEST_CASE("MODE_STACK reference is split and de-duplicated", "[achievements]")
{
    const auto set =
            parse({achievement("game-stack", {rule("MODE_STACK", "GE", 3, "multiball,wizard,multiball")})});
    REQUIRE(set.size() == 1);
    CHECK(set.all().front().rules[0].stackModes == std::vector<std::string> {"multiball", "wizard"});
}

TEST_CASE("Malformed and unevaluable definitions are skipped, the rest kept", "[achievements]")
{
    const auto set = parse({
            achievement("game-good", {rule("MODE", "GE", 1, "multiball")}),
            json {{"key", "game-no-scope"}, {"evaluation", "in_session"}, {"rules", json::array()}},
            achievement("game-unknown-type", {rule("PROGRESS", "GE", 1)}),
            achievement("game-strict-gt", {rule("SCORE", ">", 1)}),
            achievement("game-target-string", {json {{"type", "SCORE"}, {"comparison", "GE"},
                                                     {"target", "big"}}}),
            achievement("game-no-rules", {}),
            achievement("game-mode-no-ref", {rule("MODE", "GE", 1)}),
            achievement("game-stack-of-one", {rule("MODE_STACK", "GE", 1, "multiball")}),
            achievement("game-lonely-ball", {rule("BALL", "EQ", 1)}),
            achievement("game-ball-with-score",
                        {rule("SCORE", "GE", 1), rule("BALL", "EQ", 1)}),
            achievement("game-between-ok",
                        {rule("MODE", "GE", 2, "multiball"), rule("TIMER_BETWEEN", "LE", 30000)}),
            json("not an object"),
    });

    CHECK(set.size() == 2);
    CHECK(set.contains("game-good"));
    CHECK(set.contains("game-between-ok"));
}

TEST_CASE("Achievements depending on an ACHIEVEMENT cycle are skipped", "[achievements]")
{
    const auto set = parse({
            achievement("a", {rule("ACHIEVEMENT", "GE", 1, "b")}, "unlimited"),
            achievement("b", {rule("ACHIEVEMENT", "GE", 1, "a")}, "unlimited"),
            achievement("c", {rule("ACHIEVEMENT", "GE", 1, "a")}, "unlimited"),
            achievement("d", {rule("ACHIEVEMENT", "GE", 1, "e")}, "unlimited"),
            achievement("e", {rule("MODE", "GE", 1, "multiball")}),
            achievement("f", {rule("ACHIEVEMENT", "GE", 1, "elsewhere")}, "unlimited"),
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
    CHECK_FALSE(parseDefinitionsResponse(json::array()));
    CHECK_FALSE(parseDefinitionsResponse(json {{"results", "nope"}}));
}

TEST_CASE("Parse a sparse progress response", "[achievements]")
{
    const json document = {
            {"user_id", "3f1c"},
            {"results",
             {{{"key", "game-cv-marathon"},
               {"achieved", false},
               {"rule_progress",
                {{{"index", 0}, {"value", 7400}, {"target", 10000}, {"satisfied", false}},
                 {{"index", 1}, {"value", nullptr}, {"satisfied", nullptr}}}}},
              {{"key", "game-cv-boom"}, {"achieved", true}, {"rule_progress", nullptr}},
              {{"achieved", true}}}},
    };

    const auto baselines = parseProgressResponse(document);
    REQUIRE(baselines);
    REQUIRE(baselines->size() == 2);

    const auto &marathon = baselines->at("game-cv-marathon");
    CHECK_FALSE(marathon.achieved);
    REQUIRE(marathon.rules.size() == 1); // index 1 was never reported
    CHECK(marathon.rules.at(0) == RuleProgress {7400, false});

    CHECK(baselines->at("game-cv-boom").achieved);
    CHECK(baselines->at("game-cv-boom").rules.empty());
}

TEST_CASE("An empty progress response means nothing started", "[achievements]")
{
    const auto baselines = parseProgressResponse({{"user_id", "3f1c"}, {"results", json::array()}});
    REQUIRE(baselines);
    CHECK(baselines->empty());
}
