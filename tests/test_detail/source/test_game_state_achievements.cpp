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

/**
 * Covers GameStateImpl's achievement wiring: that the two fetch methods both refresh the local
 * AchievementManager *and* still call the caller back, that unlock/lock are pure passthroughs to
 * NetBase, and that the synchronous matchers work without touching the network at all.
 *
 * A user id is the public user UUID (the "id" exposed everywhere else in the v2 API), a string -
 * not the internal integer id the platform also has. Tests below use small string literals
 * ("42", "99", ...) as stand-ins; any opaque string works, since neither GameStateImpl nor
 * AchievementManager validates UUID format.
 *
 * Net's JSON parsing is deliberately not covered here - net.cpp is excluded from this test target
 * (see the commented-out `../../source/net.cpp` in CMakeLists.txt), so the wire-format parsing in
 * Net::fetchAchievements and friends has no direct unit coverage yet.
 */

#include "game_state_impl.h"
#include "net_base.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

namespace {

/// Minimal NetBase that mocks only the four achievement transports and no-ops everything else.
class MockAchievementNet : public NetBase
{
public:
    AuthStatus status() const override { return AuthStatus::AuthenticatedPaired; }
    void requestPairCode(StringCallback) override { }
    const std::string &getMachineUuid() const override
    {
        static std::string rv;
        return rv;
    }
    std::uint64_t getMachineSerial() const override { return 0; }
    const std::string &getPairDeeplink() const override
    {
        static std::string rv;
        return rv;
    }
    const DeviceInfo &deviceInfo() const override
    {
        static DeviceInfo info;
        return info;
    }
    void requestTopScores(LeaderboardScope, LeaderboardPeriod, const std::string &,
                          LeaderboardVpinFilter, LeaderboardHandleCallback) override
    {
    }
    void requestUnpair(StringCallback) override { }
    void submitGameData(const GameData &, SessionFlags) override { }
    void authenticate() override { }
    void sessionCreate(const GameData &, GameStartOrigin, std::function<void()>) override { }
    void getConfig() override { }
    void updateConfig(const std::string &, const std::string &, bool,
                      std::optional<std::string>) override
    {
    }
    void download(bool, StringCallback, const std::string &, const std::string &,
                  const HttpHeaders &) override
    {
    }
    PlayerProfilesManager &playersManager() override { return m_playersManager; }
    void patchScorbitron(std::string, StringCallback, std::vector<AuthStatus>) override { }
    std::string consumeNonce() override { return {}; }
    void requestPairMachine(const std::string &, const std::string &, StringCallback) override { }
    void setCapabilities(Capabilities) override { }
    void setCreditsDropped(int, const std::string &, bool) override { }
    void setCreditsStatus(bool, int, int, const char *) override { }

    MAKE_MOCK1(fetchAchievements, void(AchievementsCallback), override);
    MAKE_MOCK2(fetchAchievementProgress, void(const std::string &, AchievementProgressCallback),
               override);
    MAKE_MOCK4(unlockAchievement,
               void(const std::string &, const std::string &, int, AchievementUnlockCallback),
               override);
    MAKE_MOCK3(lockAchievement,
               void(const std::string &, const std::string &, AchievementUnlockCallback),
               override);

    /// Records each downloadBuffer request so DMD frame downloads can be driven synchronously.
    void downloadBuffer(bool, VectorCallback callback, const std::string &url, size_t,
                        const HttpHeaders &) override
    {
        downloadRequests.emplace_back(url, std::move(callback));
    }

    std::vector<std::pair<std::string, VectorCallback>> downloadRequests;

private:
    PlayerProfilesManager m_playersManager;
};

/// A "reach 2,000,000 during Grand Finale" achievement: one MODE rule ANDed with one SCORE rule.
Achievement makeModeAndScoreAchievement()
{
    Achievement ach;
    ach.key = "grand-finale-2m";
    ach.name = "Grand Finale";
    ach.scope = "game";
    ach.imageUrl = "https://example.test/grand-finale.png";
    ach.rules = {
            AchievementRule {"MODE", ">", 0, "Grand Finale", 0},
            AchievementRule {"SCORE", ">", 2000000, "", 0},
    };
    return ach;
}

/// A plain "beat 1,000,000" achievement.
Achievement makeScoreAchievement()
{
    Achievement ach;
    ach.key = "score-1m";
    ach.name = "Millionaire";
    ach.scope = "game";
    ach.rules = {AchievementRule {"SCORE", ">", 1000000, "", 0}};
    return ach;
}

} // namespace

// =============================================================================

TEST_CASE("fetchAchievements populates the local cache and calls the caller back")
{
    auto mockNet = std::make_unique<MockAchievementNet>();
    auto &net = *mockNet;

    AchievementsCallback captured;
    REQUIRE_CALL(net, fetchAchievements(ANY(AchievementsCallback)))
            .LR_SIDE_EFFECT(captured = _1)
            .TIMES(1);

    GameStateImpl gameState(std::move(mockNet));

    std::optional<Error> callerError;
    std::vector<Achievement> callerAchievements;
    gameState.fetchAchievements([&](Error error, std::vector<Achievement> achievements) {
        callerError = error;
        callerAchievements = std::move(achievements);
    });

    // Nothing is cached until the network replies.
    REQUIRE_FALSE(gameState.hasAchievements());
    REQUIRE(captured);

    captured(Error::Success, {makeScoreAchievement(), makeModeAndScoreAchievement()});

    SECTION("the caller's callback receives the definitions")
    {
        REQUIRE(callerError == Error::Success);
        REQUIRE(callerAchievements.size() == 2);
        CHECK(callerAchievements[0].key == "score-1m");
        CHECK(callerAchievements[1].key == "grand-finale-2m");
    }

    SECTION("the local cache is refreshed, so local matching now works")
    {
        REQUIRE(gameState.hasAchievements());
        CHECK(gameState.getCachedAchievements().size() == 2);
        REQUIRE(gameState.getCachedAchievement("score-1m").has_value());

        // Score-only match: the mode+score achievement is withheld because its MODE rule cannot be
        // judged without a mode event.
        const auto matched = gameState.checkScoreAchievements(1500000, "42");
        REQUIRE(matched == std::vector<std::string> {"score-1m"});

        // With the mode event, and a score over both thresholds, both qualify: the mode+score
        // achievement now has its MODE rule satisfied by the event, and score-1m's lone SCORE
        // rule is evaluable at this entry point too (it takes a score alongside the mode event).
        const auto matchedWithMode = gameState.checkModeAchievementsWithScore(
                "Grand Finale", "complete", "42", 2500000);
        REQUIRE(matchedWithMode == std::vector<std::string> {"score-1m", "grand-finale-2m"});
    }
}

TEST_CASE("fetchAchievements failure leaves the cache untouched")
{
    auto mockNet = std::make_unique<MockAchievementNet>();
    auto &net = *mockNet;

    AchievementsCallback captured;
    REQUIRE_CALL(net, fetchAchievements(ANY(AchievementsCallback)))
            .LR_SIDE_EFFECT(captured = _1)
            .TIMES(1);

    GameStateImpl gameState(std::move(mockNet));

    std::optional<Error> callerError;
    gameState.fetchAchievements(
            [&](Error error, std::vector<Achievement>) { callerError = error; });

    captured(Error::ApiError, {});

    CHECK(callerError == Error::ApiError);
    CHECK_FALSE(gameState.hasAchievements());
}

TEST_CASE("fetchAchievementProgress populates the local cache and calls the caller back")
{
    auto mockNet = std::make_unique<MockAchievementNet>();
    auto &net = *mockNet;

    AchievementsCallback capturedAchievements;
    AchievementProgressCallback capturedProgress;
    std::string requestedUserId;

    REQUIRE_CALL(net, fetchAchievements(ANY(AchievementsCallback)))
            .LR_SIDE_EFFECT(capturedAchievements = _1)
            .TIMES(1);
    REQUIRE_CALL(net, fetchAchievementProgress(ANY(std::string), ANY(AchievementProgressCallback)))
            .LR_SIDE_EFFECT(requestedUserId = _1)
            .LR_SIDE_EFFECT(capturedProgress = _2)
            .TIMES(1);

    GameStateImpl gameState(std::move(mockNet));

    gameState.fetchAchievements({});
    capturedAchievements(Error::Success, {makeScoreAchievement()});

    std::optional<Error> callerError;
    std::vector<AchievementProgress> callerProgress;
    gameState.fetchAchievementProgress("42", [&](Error error,
                                                 std::vector<AchievementProgress> progress) {
        callerError = error;
        callerProgress = std::move(progress);
    });

    CHECK(requestedUserId == "42");
    REQUIRE(capturedProgress);

    capturedProgress(Error::Success,
                     {AchievementProgress {"score-1m", 1, true, "2026-01-01T00:00:00Z"}});

    SECTION("the caller's callback receives the progress")
    {
        REQUIRE(callerError == Error::Success);
        REQUIRE(callerProgress.size() == 1);
        CHECK(callerProgress[0].key == "score-1m");
        CHECK(callerProgress[0].unlocked);
    }

    SECTION("the local cache is refreshed and gates re-matching")
    {
        const auto cached = gameState.getCachedProgress("42", "score-1m");
        REQUIRE(cached.has_value());
        CHECK(cached->progress == 1);
        CHECK(cached->unlocked);

        // The achievement is lifetime-scoped and already unlocked for user "42", so it is skipped.
        CHECK(gameState.checkScoreAchievements(1500000, "42").empty());

        // A different user has no progress cached, so it still matches for them.
        CHECK(gameState.checkScoreAchievements(1500000, "99")
              == std::vector<std::string> {"score-1m"});
    }

    SECTION("clearing the user's progress makes it eligible again")
    {
        gameState.clearUserProgress("42");
        CHECK_FALSE(gameState.getCachedProgress("42", "score-1m").has_value());
        CHECK(gameState.checkScoreAchievements(1500000, "42")
              == std::vector<std::string> {"score-1m"});
    }
}

TEST_CASE("unlockAchievement passes through to NetBase and propagates the result")
{
    auto mockNet = std::make_unique<MockAchievementNet>();
    auto &net = *mockNet;

    AchievementUnlockCallback captured;
    REQUIRE_CALL(net, unlockAchievement("42", "score-1m", 3, ANY(AchievementUnlockCallback)))
            .LR_SIDE_EFFECT(captured = _4)
            .TIMES(1);

    GameStateImpl gameState(std::move(mockNet));

    std::optional<Error> callerError;
    AchievementUnlockResult callerResult;
    gameState.unlockAchievement("42", "score-1m", 3,
                               [&](Error error, AchievementUnlockResult result) {
                                   callerError = error;
                                   callerResult = std::move(result);
                               });

    REQUIRE(captured);
    captured(Error::Success, AchievementUnlockResult {"score-1m", true, true, {}});

    CHECK(callerError == Error::Success);
    CHECK(callerResult.key == "score-1m");
    CHECK(callerResult.success);
    CHECK(callerResult.newlyUnlocked);
    // The server response has no message field, so this is always empty.
    CHECK(callerResult.message.empty());
}

TEST_CASE("unlockAchievement does not touch the local progress cache")
{
    auto mockNet = std::make_unique<MockAchievementNet>();
    auto &net = *mockNet;

    AchievementsCallback capturedAchievements;
    AchievementUnlockCallback capturedUnlock;
    REQUIRE_CALL(net, fetchAchievements(ANY(AchievementsCallback)))
            .LR_SIDE_EFFECT(capturedAchievements = _1)
            .TIMES(1);
    REQUIRE_CALL(net, unlockAchievement("42", "score-1m", 1, ANY(AchievementUnlockCallback)))
            .LR_SIDE_EFFECT(capturedUnlock = _4)
            .TIMES(1);

    GameStateImpl gameState(std::move(mockNet));

    gameState.fetchAchievements({});
    capturedAchievements(Error::Success, {makeScoreAchievement()});

    gameState.unlockAchievement("42", "score-1m", 1,
                               [](Error, AchievementUnlockResult) { /* result ignored */ });
    capturedUnlock(Error::Success, AchievementUnlockResult {"score-1m", true, true, {}});

    // The unlock reply is deliberately not folded into the cache; that arrives via the
    // AchievementUnlocked event instead.
    CHECK_FALSE(gameState.getCachedProgress("42", "score-1m").has_value());
    CHECK(gameState.checkScoreAchievements(1500000, "42") == std::vector<std::string> {"score-1m"});
}

TEST_CASE("lockAchievement passes through to NetBase and propagates the result")
{
    auto mockNet = std::make_unique<MockAchievementNet>();
    auto &net = *mockNet;

    AchievementUnlockCallback captured;
    REQUIRE_CALL(net, lockAchievement("7", "trophy-key", ANY(AchievementUnlockCallback)))
            .LR_SIDE_EFFECT(captured = _3)
            .TIMES(1);

    GameStateImpl gameState(std::move(mockNet));

    std::optional<Error> callerError;
    AchievementUnlockResult callerResult;
    gameState.lockAchievement("7", "trophy-key", [&](Error error, AchievementUnlockResult result) {
        callerError = error;
        callerResult = std::move(result);
    });

    REQUIRE(captured);
    captured(Error::Success, AchievementUnlockResult {"trophy-key", true, false, {}});

    CHECK(callerError == Error::Success);
    CHECK(callerResult.key == "trophy-key");
    CHECK(callerResult.success);
    CHECK_FALSE(callerResult.newlyUnlocked);
}

TEST_CASE("synchronous achievement methods never touch the network")
{
    // No REQUIRE_CALL / ALLOW_CALL at all: any call into the mocked transports would fail the test.
    auto mockNet = std::make_unique<MockAchievementNet>();
    GameStateImpl gameState(std::move(mockNet));

    SECTION("an empty cache matches nothing and reports nothing")
    {
        CHECK_FALSE(gameState.hasAchievements());
        CHECK(gameState.getCachedAchievements().empty());
        CHECK_FALSE(gameState.getCachedAchievement("nope").has_value());
        CHECK_FALSE(gameState.getCachedProgress("1", "nope").has_value());
        CHECK(gameState.checkModeAchievements("Grand Finale", "complete", "1").empty());
        CHECK(gameState.checkModeAchievementsWithScore("Grand Finale", "complete", "1", 1).empty());
        CHECK(gameState.checkScoreAchievements(1, "1").empty());
        CHECK_FALSE(gameState.hasDmdFrame("nope"));
        CHECK(gameState.getDmdFrame("nope").empty());
    }

    SECTION("incrementProgress on an unrecognized achievement records nothing")
    {
        // AchievementManager::incrementProgress (already covered at the unit level in
        // test_achievement_manager.cpp) looks up the definition first and bails out before
        // touching progress or the triggered callback at all when the key is unknown - there is
        // no cached definition here (this TEST_CASE never fetches), so that is exactly what
        // should happen. Threshold-crossing behavior needs a real cached PROGRESS rule, which
        // means a real (mocked) fetch first - see "incrementProgress reports crossing the
        // PROGRESS threshold once the definition is cached" below, since this TEST_CASE is
        // deliberately network-call-free.
        std::vector<std::tuple<std::string, std::string, bool, int>> triggered;
        gameState.setAchievementTriggeredCallback(
                [&](const std::string &key, const std::string &userId, bool isUnlock,
                   int progress) { triggered.emplace_back(key, userId, isUnlock, progress); });

        CHECK_FALSE(gameState.incrementProgress("ten-ramps", "5", 1, "ramps"));
        CHECK(triggered.empty());
        CHECK_FALSE(gameState.getCachedProgress("5", "ten-ramps").has_value());
    }
}

TEST_CASE("incrementProgress reports crossing the PROGRESS threshold once the definition is cached")
{
    auto mockNet = std::make_unique<MockAchievementNet>();
    auto &net = *mockNet;

    AchievementsCallback captured;
    REQUIRE_CALL(net, fetchAchievements(ANY(AchievementsCallback)))
            .LR_SIDE_EFFECT(captured = _1)
            .TIMES(1);

    GameStateImpl gameState(std::move(mockNet));

    gameState.fetchAchievements({});
    // A counter achievement whose threshold lives on its PROGRESS rule's target.
    Achievement counter;
    counter.key = "ten-ramps";
    counter.name = "Ramp Runner";
    counter.scope = "game";
    counter.rules = {AchievementRule {"PROGRESS", ">", 2, "ramps", 0}};
    captured(Error::Success, {counter});

    std::vector<std::tuple<std::string, std::string, bool, int>> triggered;
    gameState.setAchievementTriggeredCallback(
            [&](const std::string &key, const std::string &userId, bool isUnlock, int progress) {
                triggered.emplace_back(key, userId, isUnlock, progress);
            });

    // Target is 2 with a strict ">" comparison, so it takes progress = 3, not 2, to unlock.
    CHECK_FALSE(gameState.incrementProgress("ten-ramps", "5", 1, "ramps")); // progress -> 1
    CHECK_FALSE(gameState.incrementProgress("ten-ramps", "5", 1, "ramps")); // progress -> 2
    CHECK(gameState.incrementProgress("ten-ramps", "5", 1, "ramps"));       // progress -> 3, unlocks

    REQUIRE(triggered.size() == 3);
    CHECK_FALSE(std::get<2>(triggered[0]));
    CHECK_FALSE(std::get<2>(triggered[1]));
    CHECK(std::get<2>(triggered[2]));
    CHECK(std::get<3>(triggered[2]) == 3);

    const auto cached = gameState.getCachedProgress("5", "ten-ramps");
    REQUIRE(cached.has_value());
    CHECK(cached->progress == 3);
    CHECK(cached->unlocked);
}

TEST_CASE("clearAllProgress drops every user's progress")
{
    auto mockNet = std::make_unique<MockAchievementNet>();
    auto &net = *mockNet;

    AchievementsCallback captured;
    REQUIRE_CALL(net, fetchAchievements(ANY(AchievementsCallback)))
            .LR_SIDE_EFFECT(captured = _1)
            .TIMES(1);

    GameStateImpl gameState(std::move(mockNet));

    gameState.fetchAchievements({});
    Achievement counter;
    counter.key = "a";
    counter.name = "A";
    counter.scope = "game";
    counter.rules = {AchievementRule {"PROGRESS", ">", 100, "widgets", 0}};
    captured(Error::Success, {counter});

    gameState.incrementProgress("a", "1", 1, "widgets");
    gameState.incrementProgress("a", "2", 1, "widgets");
    REQUIRE(gameState.getCachedProgress("1", "a").has_value());
    REQUIRE(gameState.getCachedProgress("2", "a").has_value());

    gameState.clearAllProgress();
    CHECK_FALSE(gameState.getCachedProgress("1", "a").has_value());
    CHECK_FALSE(gameState.getCachedProgress("2", "a").has_value());
}

TEST_CASE("downloadAchievementFrames fetches artwork for cached definitions and caches the frame")
{
    auto mockNet = std::make_unique<MockAchievementNet>();
    auto &net = *mockNet;

    AchievementsCallback captured;
    REQUIRE_CALL(net, fetchAchievements(ANY(AchievementsCallback)))
            .LR_SIDE_EFFECT(captured = _1)
            .TIMES(1);

    GameStateImpl gameState(std::move(mockNet));

    gameState.fetchAchievements({});
    // Only makeModeAndScoreAchievement() has an imageUrl, so only it is requested.
    captured(Error::Success, {makeScoreAchievement(), makeModeAndScoreAchievement()});

    gameState.downloadAchievementFrames();

    REQUIRE(net.downloadRequests.size() == 1);
    CHECK(net.downloadRequests[0].first == "https://example.test/grand-finale.png");

    net.downloadRequests[0].second(Error::Success, std::vector<uint8_t> {1, 2, 3});

    CHECK(gameState.hasDmdFrame("grand-finale-2m"));
    CHECK(gameState.getDmdFrame("grand-finale-2m") == DmdFrame {1, 2, 3});

    // A frame that is already cached is not requested again.
    net.downloadRequests.clear();
    gameState.downloadAchievementFrames();
    CHECK(net.downloadRequests.empty());
}
