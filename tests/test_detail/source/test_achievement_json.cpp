/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

/*
 * Parser tests for GET api/v2/achievements/scorbitron/.
 *
 * The fixture below is a verbatim capture from a local v2 API, not a hand-written approximation.
 * That matters: the defect these tests guard against was invisible to hand-built payloads.
 *
 * The v2 serialiser emits null for every unset optional field. An achievement created without
 * artwork, grouping, tiering or ball/duration limits comes back with seven nulls plus a null
 * subachievement on its rule. nlohmann's json::value() only returns its fallback when a key is
 * ABSENT -- present-but-null still attempts the conversion and throws type_error.302. Since the
 * whole array is parsed inside one try, a single null aborted everything and the device received
 * zero achievements from an otherwise valid response.
 */

#include "achievement_json.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace scorbit;
using namespace scorbit::detail;

namespace {

// Verbatim capture: achievement created with only name/description/scope/rule set.
// NB: custom delimiter is required -- the captured name contains "(localdev)", and the ")\"" in
// that would close a plain R"(...)" literal early.
constexpr auto REAL_PAYLOAD_WITH_NULLS = R"JSON([
    {
        "id": "01a0128d-40e0-799e-90ab-5871da805679",
        "key": "localdev-olympus-mb-start",
        "name": "Multiball Initiate (localdev)",
        "description": "Start multiball",
        "icon": null,
        "is_single_session": false,
        "is_trophy": false,
        "is_badge": false,
        "scope": "game",
        "visible": true,
        "obscure": false,
        "obscure_image": null,
        "group_id": null,
        "level": null,
        "display_position": null,
        "frame": null,
        "frame_version": 0,
        "ball_count": null,
        "duration": null,
        "notify_when_achieved": false,
        "rules": [
            {
                "type": "MODE_START",
                "comparison": ">",
                "target": 0,
                "reference": "MB:Multiball",
                "subachievement": null
            }
        ]
    }
])JSON";

} // namespace

TEST_CASE("A real API payload full of nulls parses instead of aborting", "[achievements][json]")
{
    const auto j = nlohmann::json::parse(REAL_PAYLOAD_WITH_NULLS);

    std::vector<Achievement> parsed;
    REQUIRE_NOTHROW(parsed = parseAchievements(j));

    // The regression: this used to be 0, because one null threw and took the whole array with it.
    REQUIRE(parsed.size() == 1);

    const auto &ach = parsed.front();

    SECTION("populated fields survive")
    {
        CHECK(ach.key == "localdev-olympus-mb-start");
        CHECK(ach.name == "Multiball Initiate (localdev)");
        CHECK(ach.description == "Start multiball");
        CHECK(ach.scope == "game");
        CHECK(ach.visible);
        CHECK_FALSE(ach.obscure);
        CHECK_FALSE(ach.isTrophy);
        CHECK_FALSE(ach.notifyWhenAchieved);
        CHECK(ach.inputTime == AchievementInputTime::Unlimited);
    }

    SECTION("null fields fall back rather than throwing")
    {
        CHECK(ach.imageUrl.empty());        // "icon": null
        CHECK(ach.obscureImageUrl.empty()); // "obscure_image": null
        CHECK(ach.groupId == 0);            // "group_id": null
        CHECK(ach.level == 0);              // "level": null
        CHECK(ach.ballCount == 0);          // "ball_count": null
    }

    SECTION("the MODE_START rule is intact")
    {
        REQUIRE(ach.rules.size() == 1);
        const auto &rule = ach.rules.front();
        CHECK(rule.type == "MODE_START");
        CHECK(rule.comparison == ">");
        CHECK(rule.target == 0);
        CHECK(rule.reference == "MB:Multiball");
        CHECK(rule.subachievementId == 0); // "subachievement": null
    }

    SECTION("flat convenience fields are derived from the primary rule")
    {
        CHECK(ach.trigger == AchievementTrigger::Mode);
        CHECK(ach.modeType == AchievementModeType::Start);
        CHECK(ach.modeName == "MB:Multiball");
    }
}

TEST_CASE("Null handling matches absent-key handling exactly", "[achievements][json]")
{
    // The two payloads differ only in whether the optional keys are present-as-null or missing.
    // They must parse identically; that equivalence is the whole contract of the null handling.
    const auto withNulls = nlohmann::json::parse(R"([
        {"key":"k","name":"n","scope":"game","icon":null,"obscure_image":null,
         "group_id":null,"level":null,"ball_count":null,
         "rules":[{"type":"SCORE","comparison":">","target":1000,"reference":null,
                   "subachievement":null}]}
    ])");
    const auto withoutKeys = nlohmann::json::parse(R"([
        {"key":"k","name":"n","scope":"game",
         "rules":[{"type":"SCORE","comparison":">","target":1000}]}
    ])");

    const auto a = parseAchievements(withNulls);
    const auto b = parseAchievements(withoutKeys);

    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(a.front().imageUrl == b.front().imageUrl);
    CHECK(a.front().obscureImageUrl == b.front().obscureImageUrl);
    CHECK(a.front().groupId == b.front().groupId);
    CHECK(a.front().level == b.front().level);
    CHECK(a.front().ballCount == b.front().ballCount);
    REQUIRE(a.front().rules.size() == 1);
    REQUIRE(b.front().rules.size() == 1);
    CHECK(a.front().rules.front().reference == b.front().rules.front().reference);
    CHECK(a.front().rules.front().subachievementId == b.front().rules.front().subachievementId);

    // And the flat fields agree too.
    CHECK(a.front().trigger == b.front().trigger);
    CHECK(a.front().targetScore == b.front().targetScore);
}

TEST_CASE("A rule target beyond INT32_MAX parses without truncating", "[achievements][json]")
{
    // A "SCORE" rule's target is a pinball score, so it is parsed as int64_t. Parsed as int it
    // would wrap: 10,000,000,000 becomes 1,410,065,408, silently arming the achievement roughly
    // seven times too early. The server's Rule.target is still a 32-bit PositiveIntegerField, so
    // this payload does not occur in production yet - the test pins the SDK side so it stays
    // correct when the API widens the field, and so debug-seeded definitions behave.
    const auto j = nlohmann::json::parse(R"([
        {"key":"big","name":"Ten Billion","scope":"game",
         "rules":[{"type":"SCORE","comparison":">","target":10000000000,"reference":"score"}]}
    ])");

    const auto parsed = parseAchievements(j);
    REQUIRE(parsed.size() == 1);
    REQUIRE(parsed.front().rules.size() == 1);
    CHECK(parsed.front().rules.front().target == 10'000'000'000);

    // The flat convenience field is derived from the same value and must not narrow either.
    CHECK(parsed.front().targetScore == 10'000'000'000);
}

TEST_CASE("A genuine type mismatch still throws", "[achievements][json]")
{
    // Null tolerance must not become "swallow anything". A string where a number belongs is real
    // contract breakage and should stay loud.
    const auto j = nlohmann::json::parse(R"([{"key":"k","name":"n","group_id":"not-a-number"}])");
    CHECK_THROWS_AS(parseAchievements(j), nlohmann::json::exception);
}

TEST_CASE("Malformed entries are skipped, not fatal", "[achievements][json]")
{
    const auto j = nlohmann::json::parse(R"([
        "a bare string, not an object",
        {"key":"good","name":"Good","scope":"game",
         "rules":[{"type":"MODE_START","reference":"MB:Multiball"}]}
    ])");

    const auto parsed = parseAchievements(j);
    REQUIRE(parsed.size() == 1);
    CHECK(parsed.front().key == "good");
}

TEST_CASE("A non-array payload yields nothing rather than throwing", "[achievements][json]")
{
    const auto j = nlohmann::json::parse(R"({"detail":"Scorbitron not paired to a machine"})");
    std::vector<Achievement> parsed;
    REQUIRE_NOTHROW(parsed = parseAchievements(j));
    CHECK(parsed.empty());
}
