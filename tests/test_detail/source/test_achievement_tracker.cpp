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
#include <../source/achievements/json_codec.h>
#include <../source/achievements/player_tracker.h>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>

// clazy:excludeall=non-pod-global-static

using namespace achievement_test;

namespace {

constexpr auto GE = Comparison::Ge;

const ReportItem *findItem(const std::vector<ReportItem> &items, const std::string &key)
{
    const auto it = std::ranges::find(items, key, &ReportItem::key);
    return it == items.end() ? nullptr : &*it;
}

std::vector<AchievementStatus> statusesOf(const std::vector<AchievementUpdate> &updates,
                                          const std::string &key)
{
    std::vector<AchievementStatus> rv;
    for (const auto &u : updates) {
        if (u.key == key) {
            rv.push_back(u.status);
        }
    }
    return rv;
}

/** Plays the role of the service: one player, one set of definitions. */
struct Session {
    Session(std::vector<Definition> defs, Baselines baselines = {})
        : definitions {std::move(defs)}
        , tracker {"3f1c", std::move(baselines)}
    {
        timeline.at(0).commit();
        tracker.reevaluate({timeline.facts, 1}, definitions);
    }

    void commit()
    {
        timeline.commit();
        tracker.reevaluate({timeline.facts, 1}, definitions);
    }

    void outcome(const ReportItem &item, ReportStatus status)
    {
        tracker.applyOutcome(item, {item.key, status, {}}, {timeline.facts, 1}, definitions);
    }

    Timeline timeline;
    DefinitionSet definitions;
    PlayerTracker tracker;
};

} // namespace

TEST_CASE("The ball-end report worked example (§7.7)", "[achievements]")
{
    Baselines baselines;
    baselines["game-cv-marathon"] = Baseline {false, {{0, RuleProgress {7400, false}}}};

    Session s({unlimited("game-cv-marathon", {rule(RuleType::Event, GE, 10000, "distance")}),
               unlimited("game-cv-spin-master", {rule(RuleType::Event, GE, 1000, "spins")})},
              std::move(baselines));

    // Ball 1: A gains 900, B gains 120
    s.timeline.at(1000).event("distance", 900).event("spins", 120);
    s.commit();
    s.timeline.at(2000).ball(2);
    s.commit();

    auto report = s.tracker.collectReport();
    REQUIRE(report.size() == 2);
    const auto *a = findItem(report, "game-cv-marathon");
    const auto *b = findItem(report, "game-cv-spin-master");
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->rules.at(0) == RuleProgress {8300, false}); // lifetime figure, not the ball's 900
    CHECK(a->achieved == false);
    CHECK(b->rules.at(0) == RuleProgress {120, false});
    CHECK(b->achieved == false);
    s.outcome(*a, ReportStatus::InProgress);
    s.outcome(*b, ReportStatus::InProgress);
    CHECK(statusesOf(s.tracker.takeUpdates(), "game-cv-marathon")
          == std::vector {AchievementStatus::Progress});

    // Ball 2: A gains 1800 and crosses the target, B gains 80
    s.timeline.at(3000).event("distance", 1800).event("spins", 80);
    s.commit();
    CHECK(statusesOf(s.tracker.takeUpdates(), "game-cv-marathon")
          == std::vector {AchievementStatus::UnlockedLocally});
    CHECK(s.tracker.takeClaims().empty()); // unlimited: claimed at the ball boundary

    s.timeline.at(4000).ball(3);
    s.commit();
    report = s.tracker.collectReport();
    a = findItem(report, "game-cv-marathon");
    b = findItem(report, "game-cv-spin-master");
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->rules.at(0) == RuleProgress {10100, true});
    CHECK(a->achieved == true);
    CHECK(b->rules.at(0) == RuleProgress {200, false});
    s.outcome(*a, ReportStatus::Unlocked);
    s.outcome(*b, ReportStatus::InProgress);
    CHECK(statusesOf(s.tracker.takeUpdates(), "game-cv-marathon")
          == std::vector {AchievementStatus::Progress, AchievementStatus::Confirmed});

    // Ball 3: A is held but keeps accumulating; B did not change
    s.timeline.at(5000).event("distance", 400);
    s.commit();
    s.timeline.at(6000).ball(4);
    s.commit();
    report = s.tracker.collectReport();
    REQUIRE(report.size() == 1);
    CHECK(report[0].key == "game-cv-marathon");
    CHECK(report[0].rules.at(0) == RuleProgress {10500, true});
    s.outcome(report[0], ReportStatus::AlreadyHeld);
    CHECK(statusesOf(s.tracker.takeUpdates(), "game-cv-marathon")
          == std::vector {AchievementStatus::Progress}); // no second celebration
}

TEST_CASE("An unlimited count carries across games: 4, then 6 more, is one award at 10",
          "[achievements]")
{
    const auto def = unlimited("game-taf-bear", {rule(RuleType::Event, GE, 10, "bear_kick")});

    // Game 1: nothing stored yet. Four kicks, then the ball ends.
    Session first({def});
    first.timeline.at(1000).event("bear_kick", 4);
    first.commit();
    first.timeline.at(2000).ball(2);
    first.commit();
    auto report = first.tracker.collectReport();
    REQUIRE(report.size() == 1);
    CHECK(report[0].rules.at(0) == RuleProgress {4, false});
    first.outcome(report[0], ReportStatus::InProgress);

    // What the API stored, served as §10.4 does, read by the production parser. Every machine the
    // player goes to next (or this one after a restart) starts from the same baseline.
    const auto stored = nlohmann::json::parse(R"({
        "user_id": "3f1c",
        "results": [ { "key": "game-taf-bear", "achieved": false, "achieved_time": null,
                       "venue": null, "machine": null, "is_holder": null, "percent": 40,
                       "rule_progress": [ { "index": 0, "value": 4, "target": 10,
                                            "satisfied": false } ] } ] })");
    const auto baselines = parseProgressResponse(stored);
    REQUIRE(baselines);

    for (const auto *machine : {"after a restart", "on another machine"}) {
        CAPTURE(machine);
        Session next({def}, *baselines);
        next.timeline.at(1000).event("bear_kick", 6);
        next.commit();
        CHECK(statusesOf(next.tracker.takeUpdates(), "game-taf-bear")
              == std::vector {AchievementStatus::UnlockedLocally});

        next.timeline.at(2000).ball(2);
        next.commit();
        report = next.tracker.collectReport();
        REQUIRE(report.size() == 1);
        CHECK(report[0].rules.at(0) == RuleProgress {10, true}); // lifetime 4 + 6, not 6
        CHECK(report[0].achieved == true);
        next.outcome(report[0], ReportStatus::Unlocked);

        // A retry of the same absolute report (an offline resend) is not a second award
        next.outcome(report[0], ReportStatus::AlreadyHeld);
        const auto statuses = statusesOf(next.tracker.takeUpdates(), "game-taf-bear");
        CHECK(std::ranges::count(statuses, AchievementStatus::Confirmed) == 1);
    }
}

TEST_CASE("A progress response captured from the API seeds the real tracker", "[achievements]")
{
    // A 200 body from GET achievements/scorbitron/progress/, as the API serves it (the user id
    // zeroed). A counter with 4 stored and no stored verdict; a two-rule achievement whose second
    // rule was never reported, so it comes back null.
    const auto captured = nlohmann::json::parse(
            R"({"user_id":"00000000-0000-0000-0000-000000000000","results":[{"key":"game-capture-lifetime-counter","achieved":false,"achieved_time":null,"venue":null,"machine":null,"is_holder":null,"percent":40,"rule_progress":[{"index":0,"value":4,"target":10,"satisfied":null}]},{"key":"game-capture-two-rules","achieved":false,"achieved_time":null,"venue":null,"machine":null,"is_holder":null,"percent":0,"rule_progress":[{"index":0,"value":2,"target":5,"satisfied":false},{"index":1,"value":null,"target":7,"satisfied":null}]}]})");

    const auto baselines = parseProgressResponse(captured);
    REQUIRE(baselines);
    REQUIRE(baselines->size() == 2);
    CHECK(baselines->at("game-capture-lifetime-counter").rules.at(0) == RuleProgress {4, false});
    const auto &twoRules = baselines->at("game-capture-two-rules");
    CHECK(twoRules.rules.at(0) == RuleProgress {2, false});
    CHECK_FALSE(twoRules.rules.contains(1)); // never reported: counts from zero

    Session s({unlimited("game-capture-lifetime-counter", {rule(RuleType::Event, GE, 10, "kick")}),
               unlimited("game-capture-two-rules",
                         {rule(RuleType::Event, GE, 5, "a"), rule(RuleType::Event, GE, 7, "b")})},
              *baselines);
    s.timeline.at(1000).event("kick", 6).event("a", 3).event("b", 7);
    s.commit();
    s.timeline.at(2000).ball(2);
    s.commit();

    const auto report = s.tracker.collectReport();
    const auto *counter = findItem(report, "game-capture-lifetime-counter");
    const auto *two = findItem(report, "game-capture-two-rules");
    REQUIRE(counter);
    REQUIRE(two);
    CHECK(counter->rules.at(0) == RuleProgress {10, true}); // 4 stored + 6
    CHECK(counter->achieved == true);
    CHECK(two->rules.at(0) == RuleProgress {5, true}); // 2 stored + 3
    CHECK(two->rules.at(1) == RuleProgress {7, true}); // nothing stored + 7
    CHECK(two->achieved == true);
}

TEST_CASE("A slot claimed again by the same player takes back only its own contribution",
          "[achievements]")
{
    const DefinitionSet definitions {
            {unlimited("game-taf-bear", {rule(RuleType::Event, GE, 10, "bear_kick")}),
             unlimited("game-started-here", {rule(RuleType::Event, GE, 10, "spins")}),
             unlimited("game-best-score", {rule(RuleType::Score, GE, 1000000)}),
             unlimited("game-two-rules",
                       {rule(RuleType::Event, GE, 10, "a"), rule(RuleType::Event, GE, 10, "b")})}};

    // First claim of the slot: 4 stored. The slot then sent 7 (4 + 3 this game), and changed hands.
    Baselines first;
    first["game-taf-bear"] = Baseline {false, {{0, RuleProgress {4, false}}}};
    first["game-best-score"] = Baseline {false, {{0, RuleProgress {800000, false}}}};
    first["game-two-rules"] = Baseline {false, {{0, RuleProgress {1, false}}, {1, {5, false}}}};
    const std::map<std::string, RuleProgressMap> sent {
            {"game-taf-bear", {{0, RuleProgress {7, false}}}},
            {"game-started-here", {{0, RuleProgress {2, false}}}},
            {"game-best-score", {{0, RuleProgress {900000, false}}}},
            {"game-two-rules", {{0, RuleProgress {3, false}}, {1, {5, false}}}}};

    SECTION("Back on the same slot: the slot's 3 come out of the fresh 7")
    {
        Baselines fresh;
        fresh["game-taf-bear"] = Baseline {false, {{0, RuleProgress {7, false}}}};
        fresh["game-started-here"] = Baseline {false, {{0, RuleProgress {2, false}}}};
        fresh["game-best-score"] = Baseline {false, {{0, RuleProgress {900000, false}}}};
        // Rule 1 never reached the server
        fresh["game-two-rules"] = Baseline {true, {{0, RuleProgress {3, false}}}};

        const auto merged = reclaimBaselines(first, fresh, sent, definitions);
        CHECK(merged.at("game-taf-bear").rules.at(0) == RuleProgress {4, false});
        CHECK(merged.at("game-started-here").rules.at(0) == RuleProgress {0, false});
        // An observation merges to a best value, so the fresh one is reused as it is
        CHECK(merged.at("game-best-score").rules.at(0) == RuleProgress {900000, false});
        CHECK(merged.at("game-two-rules").rules.at(0) == RuleProgress {1, false});
        CHECK(merged.at("game-two-rules").rules.at(1) == RuleProgress {5, false});
        CHECK(merged.at("game-two-rules").achieved); // holdings come from the fresh fetch
    }

    SECTION("Played on another slot meanwhile: that progress is kept")
    {
        // The player went to slot 2 and added 2 there (stored 9), then came back
        Baselines fresh;
        fresh["game-taf-bear"] = Baseline {false, {{0, RuleProgress {9, false}}}};
        const auto merged = reclaimBaselines(first, fresh, sent, definitions);
        // 9 - this slot's 3: plus the slot's facts (3) and one more kick, the next report is 10
        CHECK(merged.at("game-taf-bear").rules.at(0) == RuleProgress {6, false});
    }

    SECTION("A decrease made elsewhere meanwhile is kept")
    {
        // Another slot's negative event took 5 off: 4 + 3 - 5 = 2 stored. Taking this slot's 3
        // back out leaves -1, which with the slot's facts (3) reports 2, as stored.
        Baselines fresh;
        fresh["game-taf-bear"] = Baseline {false, {{0, RuleProgress {2, false}}}};
        const auto merged = reclaimBaselines(first, fresh, sent, definitions);
        CHECK(merged.at("game-taf-bear").rules.at(0) == RuleProgress {-1, false});
    }
}

TEST_CASE("Each re-claim takes back only what the slot sent since its previous claim",
          "[achievements]")
{
    const DefinitionSet definitions {
            {unlimited("game-taf-bear", {rule(RuleType::Event, GE, 100, "bear_kick")})}};
    const auto one = [](int64_t value) {
        Baselines b;
        b["game-taf-bear"] = Baseline {false, {{0, RuleProgress {value, false}}}};
        return b;
    };
    const auto sentOf = [](int64_t value) {
        return std::map<std::string, RuleProgressMap> {
                {"game-taf-bear", {{0, RuleProgress {value, false}}}}};
    };

    // Claim 1 starts at 4; the slot's facts reach 3 and it sends 7. Another slot adds 2 (stored 9).
    auto used = one(4);
    used = reclaimBaselines(used, one(9), sentOf(7), definitions);
    CHECK(used.at("game-taf-bear").rules.at(0).value == 6); // 9 - (7 - 4)

    // Claim 2 starts at 6; the slot's facts reach 4 and it sends 10. Another slot adds 5 (stored
    // 15). Measured from claim 2's start, the slot added 4: 15 - 4 = 11, and 11 + 4 = 15 reports.
    used = reclaimBaselines(used, one(15), sentOf(10), definitions);
    CHECK(used.at("game-taf-bear").rules.at(0).value == 11);
}

TEST_CASE("At an int64 bound the result is the stored value plus the net delta, in any order",
          "[achievements]")
{
    const auto def = unlimited("game-max", {rule(RuleType::Event, GE, 10, "spins")});
    for (const auto &[first, second] : {std::pair {1, -1}, std::pair {-1, 1}}) {
        CAPTURE(first, second);
        Baselines baselines;
        baselines["game-max"] = Baseline {false, {{0, RuleProgress {INT64_MAX, true}}}};
        Session s({def}, std::move(baselines));
        s.timeline.at(1000).event("spins", first);
        s.commit();
        s.timeline.at(2000).event("spins", second);
        s.commit();
        const auto *record = s.tracker.record("game-max");
        REQUIRE(record);
        CHECK(record->rules.at(0).value == INT64_MAX); // a net delta of zero
    }
}

TEST_CASE("A stored value at the int64 bounds saturates instead of overflowing", "[achievements]")
{
    CHECK(addSaturating(INT64_MAX, 1) == INT64_MAX);
    CHECK(addSaturating(INT64_MIN, -1) == INT64_MIN);
    CHECK(addSaturating(INT64_MAX, -1) == INT64_MAX - 1);
    CHECK(subtractSaturating(INT64_MIN, 1) == INT64_MIN);
    CHECK(subtractSaturating(0, INT64_MIN) == INT64_MAX);
    CHECK(subtractSaturating(-1, INT64_MIN) == INT64_MAX);
    CHECK(subtractSaturating(5, 3) == 2);

    Baselines baselines;
    baselines["game-max-events"] = Baseline {false, {{0, RuleProgress {INT64_MAX, true}}}};
    baselines["game-min-events"] = Baseline {false, {{0, RuleProgress {INT64_MIN, false}}}};
    baselines["game-max-sessions"] = Baseline {false, {{0, RuleProgress {INT64_MAX, true}}}};
    Session s({unlimited("game-max-events", {rule(RuleType::Event, GE, 10, "spins")}),
               unlimited("game-min-events", {rule(RuleType::Event, GE, 10, "ramps")}),
               unlimited("game-max-sessions", {rule(RuleType::Session, GE, 10)})},
              std::move(baselines));
    s.timeline.at(1000).event("spins", 5).event("ramps", -5);
    s.commit();
    s.timeline.at(2000).ball(2);
    s.commit();

    // Held at the bound, so unchanged from what the server holds and not reported again
    CHECK(s.tracker.collectReport().empty());
    for (const auto &[key, bound] :
         {std::pair {"game-max-events", INT64_MAX}, std::pair {"game-min-events", INT64_MIN},
          std::pair {"game-max-sessions", INT64_MAX}}) {
        CAPTURE(key);
        const auto *record = s.tracker.record(key);
        REQUIRE(record);
        CHECK(record->rules.at(0).value == bound);
    }
}

TEST_CASE("Unacknowledged values ride along with the next report", "[achievements]")
{
    Session s({unlimited("game-spins", {rule(RuleType::Event, GE, 1000, "spins")})});
    s.timeline.at(1000).event("spins", 5);
    s.commit();
    CHECK(s.tracker.collectReport().size() == 1);
    // No outcome arrived: still changed
    CHECK(s.tracker.collectReport().size() == 1);
}

TEST_CASE("Nothing is reported for achievements with nothing measured", "[achievements]")
{
    Session s({unlimited("game-mode", {rule(RuleType::Mode, GE, 5, "multiball")}),
               unlimited("game-centurion", {rule(RuleType::Session, GE, 100)})});
    s.timeline.at(1000).ball(2);
    s.commit();

    const auto report = s.tracker.collectReport();
    REQUIRE(report.size() == 1);
    CHECK(report[0].key == "game-centurion"); // the game in progress counts
    CHECK(report[0].rules.at(0) == RuleProgress {1, false});
}

TEST_CASE("What the machine tracks (§7.7)", "[achievements]")
{
    Baselines baselines;
    baselines["game-held"] = Baseline {true, {{0, RuleProgress {1, true}}}};
    baselines["game-started"] = Baseline {false, {{0, RuleProgress {4, false}}}};
    baselines["game-life-held"] = Baseline {true, {{0, RuleProgress {12, true}}}};

    Session s({inSession("game-held", {rule(RuleType::Mode, GE, 1, "multiball")}),
               inSession("game-started", {rule(RuleType::Mode, GE, 5, "multiball")}),
               inSession("game-fresh", {rule(RuleType::Mode, GE, 1, "multiball")}),
               unlimited("game-life-held", {rule(RuleType::Mode, GE, 10, "multiball")})},
              std::move(baselines));

    s.timeline.at(1000).modes({"multiball"});
    s.commit();

    CHECK_FALSE(s.tracker.record("game-held")); // held in_session: not tracked
    REQUIRE(s.tracker.record("game-started"));
    CHECK(s.tracker.record("game-started")->rules.at(0).value == 1); // at zero, not 4 + 1
    REQUIRE(s.tracker.record("game-life-held"));
    CHECK(s.tracker.record("game-life-held")->rules.at(0).value == 13); // keeps accumulating

    const auto updates = s.tracker.takeUpdates();
    CHECK(statusesOf(updates, "game-fresh") == std::vector {AchievementStatus::UnlockedLocally});
    CHECK(statusesOf(updates, "game-life-held").empty()); // already held: no celebration
}

TEST_CASE("An in_session unlock is claimed at once, then settled", "[achievements]")
{
    Session s({inSession("game-boom", {rule(RuleType::Mode, GE, 1, "balloon")})});
    s.timeline.at(1000).modes({"balloon"});
    s.commit();

    const auto claims = s.tracker.takeClaims();
    REQUIRE(claims.size() == 1);
    CHECK(claims[0].key == "game-boom");
    CHECK(claims[0].achieved == true);
    CHECK(claims[0].rules.at(0) == RuleProgress {1, true});

    s.timeline.at(2000).modes({}).ball(2);
    s.commit();
    s.timeline.at(3000).modes({"balloon"});
    s.commit();
    CHECK(s.tracker.takeClaims().empty());
    CHECK(s.tracker.collectReport().empty()); // in_session records are never ball-reported
}

TEST_CASE("A trophy is never claimed, but its metric is reported", "[achievements]")
{
    auto trophy = unlimited("venue-fgw-top-jp", {rule(RuleType::Score, GE, 1)});
    trophy.isTrophy = true;
    Session s({trophy});

    s.timeline.at(1000).score(1, 5'000'000);
    s.commit();
    CHECK(s.tracker.takeUpdates().empty());
    CHECK_FALSE(s.tracker.holds("venue-fgw-top-jp"));

    const auto report = s.tracker.collectReport();
    REQUIRE(report.size() == 1);
    CHECK_FALSE(report[0].achieved.has_value());
    CHECK(report[0].rules.at(0) == RuleProgress {5'000'000, true});
}

TEST_CASE("Multiball Monarch: a MODE_SCORE trophy reports its best gain", "[achievements]")
{
    auto trophy = unlimited("game-mb-monarch", {rule(RuleType::ModeScore, GE, 1, "multiball")});
    trophy.isTrophy = true;
    Session s({trophy});

    s.timeline.at(1000).modes({"multiball"});
    s.commit();
    s.timeline.at(2000).score(1, 20'000'000).modes({});
    s.commit();
    CHECK_FALSE(s.tracker.holds("game-mb-monarch"));

    const auto report = s.tracker.collectReport();
    REQUIRE(report.size() == 1);
    CHECK_FALSE(report[0].achieved.has_value());
    CHECK(report[0].rules.at(0) == RuleProgress {20'000'000, true});
}

TEST_CASE("A record with nothing judged is not reported", "[achievements]")
{
    // e.g. a CALENDAR achievement while the clock is unsynchronised
    auto def = unlimited("game-weekend", {rule(RuleType::Calendar, GE, 1, "window=weekend")});
    Session s({def});
    s.timeline.at(1000).synchronised(false);
    s.commit();

    REQUIRE(s.tracker.record("game-weekend"));
    CHECK(s.tracker.record("game-weekend")->rules.empty());
    CHECK(s.tracker.collectReport().empty());
}

TEST_CASE("A same-title chain resolves in one pass", "[achievements]")
{
    Session s({unlimited("game-tom-devotee",
                         {rule(RuleType::Achievement, GE, 1, "game-tom-tenball"),
                          rule(RuleType::Achievement, GE, 1, "game-tom-spinner")}),
               inSession("game-tom-tenball", {rule(RuleType::Mode, GE, 1, "multiball")}),
               unlimited("game-tom-spinner", {rule(RuleType::Event, GE, 3, "spins")})});

    s.timeline.at(1000).event("spins", 3).modes({"multiball"});
    s.commit();

    const auto updates = s.tracker.takeUpdates();
    CHECK(statusesOf(updates, "game-tom-tenball")
          == std::vector {AchievementStatus::UnlockedLocally});
    CHECK(statusesOf(updates, "game-tom-spinner")
          == std::vector {AchievementStatus::UnlockedLocally});
    CHECK(statusesOf(updates, "game-tom-devotee")
          == std::vector {AchievementStatus::UnlockedLocally});
}

TEST_CASE("A prerequisite not delivered withholds the claim", "[achievements]")
{
    Session s({unlimited("global-keith-elwin-lover",
                         {rule(RuleType::Achievement, GE, 1, "game-jp-played"),
                          rule(RuleType::Achievement, GE, 1, "game-im-played")}),
               inSession("game-jp-played", {rule(RuleType::Mode, GE, 1, "multiball")})});

    s.timeline.at(1000).modes({"multiball"});
    s.commit();

    const auto report = s.tracker.collectReport();
    const auto *item = findItem(report, "global-keith-elwin-lover");
    REQUIRE(item);
    CHECK_FALSE(item->achieved.has_value());
    CHECK(item->rules.size() == 1); // rule 1 is omitted, not guessed
    CHECK(item->rules.at(0) == RuleProgress {1, true});
}

TEST_CASE("A refused unlock is retracted, and so are its dependents", "[achievements]")
{
    Session s({unlimited("game-parent", {rule(RuleType::Achievement, GE, 1, "game-child")}),
               inSession("game-child", {rule(RuleType::Mode, GE, 1, "multiball")})});

    s.timeline.at(1000).modes({"multiball"});
    s.commit();
    CHECK(s.tracker.holds("game-parent"));
    s.tracker.takeUpdates();

    const auto claims = s.tracker.takeClaims();
    REQUIRE(claims.size() == 1);
    s.outcome(claims[0], ReportStatus::Rejected);

    const auto updates = s.tracker.takeUpdates();
    CHECK(statusesOf(updates, "game-child") == std::vector {AchievementStatus::Retracted});
    CHECK(statusesOf(updates, "game-parent") == std::vector {AchievementStatus::Retracted});
    CHECK_FALSE(s.tracker.holds("game-parent"));
    CHECK_FALSE(s.tracker.holds("game-child"));
}
