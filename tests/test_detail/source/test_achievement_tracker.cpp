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
#include <../source/achievements/player_tracker.h>
#include <catch2/catch_test_macros.hpp>
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
        : definitions {"cactus-canyon", 0, std::move(defs)}
        , tracker {"3f1c", std::move(baselines)}
    {
        timeline.at(0).commit();
        tracker.reevaluate(timeline.facts.player(1), definitions);
    }

    void commit()
    {
        timeline.commit();
        tracker.reevaluate(timeline.facts.player(1), definitions);
    }

    void outcome(const ReportItem &item, ReportStatus status)
    {
        tracker.applyOutcome(item, {item.key, status, {}}, timeline.facts.player(1), definitions);
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
