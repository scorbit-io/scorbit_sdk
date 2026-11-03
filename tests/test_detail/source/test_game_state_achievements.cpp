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

#include "game_state_impl.h"
#include "achievements/artwork_store.h"
#include "achievements/json_codec.h"
#include "net_base.h"
#include "event_classes.h"

#include <catch2/catch_test_macros.hpp>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <deque>
#include <fstream>
#include <limits>
#include <set>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using json = nlohmann::json;

namespace {

/** A NetBase whose achievements requests are answered by the test. */
class FakeNet : public NetBase
{
public:
    explicit FakeNet(std::string dataDir,
                     sb_achievement_artwork_t artwork = SB_ACHIEVEMENT_ARTWORK_NONE)
    {
        m_info.dataDir = std::move(dataDir);
        m_info.achievementArtwork = artwork;
    }

    AuthStatus status() const override { return AuthStatus::AuthenticatedPaired; }
    void authenticate() override { }
    void updateConfig(const std::string &, const std::string &, bool, std::optional<std::string>,
                      HttpStatusCallback) override
    {
    }
    void sessionCreate(const GameData &, GameStartOrigin, SessionCreatedCallback cb) override
    {
        sessionCreated = std::move(cb);
    }
    void submitGameData(const GameData &, SessionFlags) override { ++rows; }
    void getConfig() override { }
    void requestPairCode(StringCallback) override { }
    const std::string &getMachineUuid() const override { return m_empty; }
    std::uint64_t getMachineSerial() const override { return 0; }
    const std::string &getPairDeeplink() const override { return m_empty; }
    const DeviceInfo &deviceInfo() const override { return m_info; }
    void requestTopScores(LeaderboardScope, LeaderboardPeriod, const std::string &,
                          LeaderboardVpinFilter, LeaderboardHandleCallback) override
    {
    }
    void requestUnpair(StringCallback) override { }
    void download(bool, StringCallback, const std::string &, const std::string &,
                  const HttpHeaders &) override
    {
    }
    void downloadBuffer(bool, VectorCallback, const std::string &, size_t,
                        const HttpHeaders &) override
    {
    }
    PlayerProfilesManager &playersManager() override { return m_players; }
    void patchScorbitron(std::string, StringCallback, std::vector<AuthStatus>) override { }
    std::string consumeNonce() override { return {}; }
    void requestPairMachine(const std::string &, const std::string &, StringCallback) override { }
    void setCapabilities(Capabilities) override { }
    void setCreditsDropped(int, const std::string &, bool) override { }
    void setCreditsStatus(bool, int, int, const char *) override { }
    void submitHardwareProbeResult(const std::string &, const std::string &,
                                   HttpStatusCallback) override
    {
    }

    void fetchAchievementDefinitions(std::string etag, ApiReplyCallback cb) override
    {
        ++definitionsFetches;
        definitionsEtag = std::move(etag);
        definitionsReply = std::move(cb);
    }
    bool isClockSynchronised() const override { return clockSynchronised; }
    void fetchAchievementProgress(std::string userId, ApiReplyCallback cb) override
    {
        progressUserId = std::move(userId);
        progressReply = std::move(cb);
    }
    void postAchievementReport(std::string body, ApiReplyCallback cb) override
    {
        reports.push_back({json::parse(body), std::move(cb)});
    }
    void downloadAchievementArtwork(std::string url, VectorCallback cb) override
    {
        artworks.push_back({std::move(url), std::move(cb)});
    }
    void scheduleAchievementRetry(std::chrono::steady_clock::duration,
                                  std::function<void()> fn) override
    {
        retry = std::move(fn);
    }
    void scheduleDefinitionsRevalidation(std::chrono::steady_clock::duration delay,
                                         std::function<void()> fn) override
    {
        revalidationDelay = delay;
        revalidate = std::move(fn);
    }
    void setPlayersChangedCallback(PlayersChangedCallback cb) override
    {
        playersChanged = std::move(cb);
    }
    void setTokenRefreshedCallback(TokenRefreshedCallback cb) override
    {
        tokenRefreshed = std::move(cb);
    }
    void publishEvent(EventPtr event) override
    {
        if (auto *e = dynamic_cast<AchievementUnlockedEvent *>(event.get())) {
            unlocks.push_back({e->key(), e->player(), e->name(), e->description(),
                               std::string(e->artwork().begin(), e->artwork().end())});
        }
    }

    void claim(sb_player_t position, const std::string &userId)
    {
        const json scores = json::array({{{"position", position},
                                          {"id", 1},
                                          {"player",
                                           {{"id", userId},
                                            {"username", "u"},
                                            {"display_name", "U"},
                                            {"initials", "UU"},
                                            {"url", ""}}}}});
        if (auto profiles = m_players.setProfiles(scores, "machine")) {
            playersChanged(*profiles);
        }
    }

    /** Every slot is unclaimed. */
    void unclaim()
    {
        if (auto profiles = m_players.setProfiles(json::array(), "machine")) {
            playersChanged(*profiles);
        }
    }

    struct Report {
        json body;
        ApiReplyCallback reply;
    };
    struct Unlock {
        std::string key;
        sb_player_t player;
        std::string name;
        std::string description;
        std::string artwork;
    };
    struct ArtworkDownload {
        std::string url;
        VectorCallback reply;

        void complete(const std::string &content) const
        {
            reply(Error::Success, std::vector<uint8_t>(content.begin(), content.end()));
        }
        void fail() const { reply(Error::ApiError, {}); }
    };

    SessionCreatedCallback sessionCreated;
    int definitionsFetches {0};
    ApiReplyCallback definitionsReply;
    std::string definitionsEtag;
    bool clockSynchronised {true};
    std::chrono::steady_clock::duration revalidationDelay {};
    std::function<void()> revalidate;
    std::string progressUserId;
    ApiReplyCallback progressReply;
    std::deque<Report> reports;
    std::function<void()> retry;
    PlayersChangedCallback playersChanged;
    TokenRefreshedCallback tokenRefreshed;
    std::vector<Unlock> unlocks;
    int rows {0};
    std::deque<ArtworkDownload> artworks;

private:
    DeviceInfo m_info;
    PlayerProfilesManager m_players;
    std::string m_empty;
};

class TempDir
{
public:
    TempDir()
        : m_path {boost::filesystem::temp_directory_path()
                  / boost::filesystem::unique_path("sb-gs-ach-%%%%-%%%%")}
    {
    }
    ~TempDir() { boost::filesystem::remove_all(m_path); }
    std::string str() const { return m_path.string(); }

private:
    boost::filesystem::path m_path;
};

const char *DEFINITIONS = R"({
  "game": "cactus-canyon", "frames_version": 0,
  "timezone": null, "utc_offsets": null, "calendar": {},
  "results": [
    {"key": "game-cv-boom", "name": "Boom Balloon", "description": "Pop the balloon.",
     "scope": "game", "is_single_session": true, "frame": null, "frame_version": 0,
     "rules": [{"type": "MODE", "comparison": ">=", "target": 1, "reference": "balloon",
                "subachievement": null}]},
    {"key": "game-cv-spins", "scope": "game", "is_single_session": false,
     "frame": null, "frame_version": 0,
     "rules": [{"type": "EVENT", "comparison": ">=", "target": 100, "reference": "spins",
                "subachievement": null}]}
  ]})";

/** A definitions response delivering @p results (§10.3). */
std::string feed(json results)
{
    auto document = json::parse(DEFINITIONS);
    document["results"] = std::move(results);
    return document.dump();
}

/** A progress entry as the API renders it. */
json progressEntry(const std::string &key, bool achieved, double currentValue)
{
    return {{"id", "6d1f2a90"},
            {"achievement", {{"id", "0b7c4f0e"}, {"key", key}, {"scope", "game"}}},
            {"current_value", currentValue},
            {"achieved", achieved},
            {"achieved_time", nullptr},
            {"progress", 0}};
}

std::string reply(const json &results)
{
    return json {{"results", results}}.dump();
}

bool hasUnlock(const FakeNet &net, const std::string &key)
{
    return std::ranges::any_of(net.unlocks, [&](const FakeNet::Unlock &u) { return u.key == key; });
}

/** Whether any report sent so far carries @p key. */
bool reported(const FakeNet &net, const std::string &key)
{
    return std::ranges::any_of(net.reports, [&](const FakeNet::Report &report) {
        return std::ranges::any_of(report.body["achievements"],
                                   [&](const json &item) { return item["key"] == key; });
    });
}

/** A reply granting, or answering otherwise, the achievement @p key of a report. */
ApiReply outcome(const std::string &key, const std::string &status)
{
    return ApiReply {Error::Success, 200, reply({{{"key", key}, {"status", status}}})};
}

/** A game state with definitions loaded, a started session and player 1 claimed. */
struct Fixture {
    TempDir dir;
    FakeNet *net;
    std::unique_ptr<GameStateImpl> game;

    explicit Fixture(json baseline = json::array(), std::string definitions = DEFINITIONS)
    {
        auto fake = std::make_unique<FakeNet>(dir.str());
        net = fake.get();
        game = std::make_unique<GameStateImpl>(std::move(fake));

        REQUIRE(net->definitionsReply);
        net->definitionsReply(ApiReply {Error::Success, 200, std::move(definitions), {}});
        game->runPendingPosts();

        game->setGameStarted(GameStartOrigin::StartButton);
        REQUIRE(net->sessionCreated);
        net->claim(1, "3f1c");
        net->sessionCreated("9ab2");
        game->runPendingPosts();

        REQUIRE(net->progressUserId == "3f1c");
        net->progressReply(ApiReply {Error::Success, 200, baseline.dump()});
        game->runPendingPosts();
    }
};

} // namespace

TEST_CASE("Definitions are cached and refetched at boot", "[achievements]")
{
    TempDir dir;
    {
        auto fake = std::make_unique<FakeNet>(dir.str());
        auto *net = fake.get();
        GameStateImpl game(std::move(fake));
        net->definitionsReply(ApiReply {Error::Success, 200, DEFINITIONS});
        game.runPendingPosts();
        CHECK(game.achievements().view()->definitions->size() == 2);
    }

    // Next boot: the cache is loaded before the network answers, then replaced by the fetch
    auto fake = std::make_unique<FakeNet>(dir.str());
    auto *net = fake.get();
    GameStateImpl game(std::move(fake));
    CHECK(net->definitionsFetches == 1);
    CHECK(game.achievements().view()->definitions->size() == 2);

    net->definitionsReply(ApiReply {Error::Success, 200, feed(json::array())});
    game.runPendingPosts();
    CHECK(game.achievements().view()->definitions->empty());
}

TEST_CASE("Definitions are revalidated with their ETag", "[achievements]")
{
    TempDir dir;
    {
        auto fake = std::make_unique<FakeNet>(dir.str());
        auto *net = fake.get();
        GameStateImpl game(std::move(fake));
        CHECK(net->definitionsEtag.empty());
        net->definitionsReply(ApiReply {Error::Success, 200, DEFINITIONS, "\"v1\""});
        game.runPendingPosts();
    }

    auto fake = std::make_unique<FakeNet>(dir.str());
    auto *net = fake.get();
    GameStateImpl game(std::move(fake));
    CHECK(net->definitionsEtag == "\"v1\"");

    SECTION("304 keeps the cached definitions")
    {
        net->definitionsReply(ApiReply {Error::Success, 304, {}});
        game.runPendingPosts();
        CHECK(game.achievements().view()->definitions->size() == 2);

        net->tokenRefreshed();
        game.runPendingPosts();
        CHECK(net->definitionsEtag == "\"v1\"");
    }

    SECTION("A changed response replaces them and its ETag")
    {
        net->definitionsReply(ApiReply {Error::Success, 200, feed(json::array()), "\"v2\""});
        game.runPendingPosts();
        CHECK(game.achievements().view()->definitions->empty());

        net->tokenRefreshed();
        game.runPendingPosts();
        CHECK(net->definitionsEtag == "\"v2\"");
    }

    SECTION("A response that doesn't parse keeps them")
    {
        net->definitionsReply(ApiReply {Error::Success, 200, "[]", "\"v3\""});
        game.runPendingPosts();
        CHECK(game.achievements().view()->definitions->size() == 2);

        net->tokenRefreshed();
        game.runPendingPosts();
        CHECK(net->definitionsEtag == "\"v1\"");
    }
}

TEST_CASE("A cache that no longer parses is discarded with its ETag", "[achievements]")
{
    // An older build cached the bare array the API used to serve
    TempDir dir;
    const auto root = boost::filesystem::path(dir.str()) / "achievements";
    boost::filesystem::create_directories(root);
    std::ofstream((root / "definitions.json").string()) << "[]";
    std::ofstream((root / "definitions.etag").string()) << "\"old\"";

    auto fake = std::make_unique<FakeNet>(dir.str());
    auto *net = fake.get();
    GameStateImpl game(std::move(fake));
    CHECK(net->definitionsEtag.empty()); // a 304 could only confirm the unusable cache
    CHECK(game.achievements().view()->definitions->empty());
    CHECK_FALSE(boost::filesystem::exists(root / "definitions.json"));
    CHECK_FALSE(boost::filesystem::exists(root / "definitions.etag"));
}

TEST_CASE("An in_session unlock is decided on commit and reported at once", "[achievements]")
{
    Fixture f;

    f.game->addMode("balloon");
    f.game->commit();

    CHECK(f.net->unlocks.empty()); // not before the server granted it
    REQUIRE(f.net->reports.size() == 1);
    const auto &body = f.net->reports.front().body;
    CHECK(body["user_id"] == "3f1c");
    CHECK(body["session_uuid"] == "9ab2");
    CHECK(body["sequence"] == 1);
    REQUIRE(body["achievements"].size() == 1);
    CHECK(body["achievements"][0]["key"] == "game-cv-boom");
    CHECK(body["achievements"][0]["achieved"] == true);

    f.net->reports.front().reply(outcome("game-cv-boom", "unlocked"));
    f.game->runPendingPosts();
    REQUIRE(f.net->unlocks.size() == 1);
    const auto &unlock = f.net->unlocks.front();
    CHECK(unlock.key == "game-cv-boom");
    CHECK(unlock.player == 1);
    CHECK(unlock.name == "Boom Balloon");
    CHECK(unlock.description == "Pop the balloon.");
    CHECK(unlock.artwork.empty()); // none was chosen
}

TEST_CASE("An achievement the player already held raises no unlock", "[achievements]")
{
    Fixture f;
    f.game->addMode("balloon");
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1);

    f.net->reports.front().reply(outcome("game-cv-boom", "already_held"));
    f.game->runPendingPosts();
    CHECK(f.net->unlocks.empty());
}

TEST_CASE("A trophy is raised when the server grants it", "[achievements]")
{
    auto definitions = json::parse(DEFINITIONS);
    definitions["results"].push_back({{"key", "game-cv-top-spinner"},
                                      {"scope", "game"},
                                      {"is_single_session", false},
                                      {"is_trophy", true},
                                      {"frame", nullptr},
                                      {"frame_version", 0},
                                      {"rules", json::array({{{"type", "EVENT"},
                                                              {"comparison", ">="},
                                                              {"target", 1},
                                                              {"reference", "spins"}}})}});
    Fixture f(json::array(), definitions.dump());

    f.game->addEvent("spins", 3);
    f.game->setCurrentBall(2);
    f.game->commit();
    CHECK(f.net->unlocks.empty()); // a trophy is never the machine's call

    REQUIRE(f.net->reports.size() == 1);
    f.net->reports.front().reply(outcome("game-cv-top-spinner", "unlocked"));
    f.game->runPendingPosts();
    REQUIRE(f.net->unlocks.size() == 1);
    CHECK(f.net->unlocks.front().key == "game-cv-top-spinner");
}

TEST_CASE("Unlimited progress is reported at the ball boundary", "[achievements]")
{
    // current_value is the legacy /unlock/ count, not a per-rule baseline
    Fixture f(json::array({progressEntry("game-cv-spins", false, 90.0)}));

    f.game->addEvent("spins", 40);
    f.game->commit();
    CHECK(f.net->reports.empty()); // not streamed

    f.game->addEvent("spins", 60);
    f.game->commit();
    CHECK(f.net->reports.empty());

    f.game->setCurrentBall(2);
    f.game->commit();

    REQUIRE(f.net->reports.size() == 1);
    const auto &item = f.net->reports.front().body["achievements"][0];
    CHECK(item["key"] == "game-cv-spins");
    CHECK(item["achieved"] == true);
    CHECK(item["rule_progress"][0]["value"] == 100); // 40 + 60
    CHECK(f.net->unlocks.empty());

    f.net->reports.front().reply(outcome("game-cv-spins", "unlocked"));
    f.game->runPendingPosts();
    CHECK(hasUnlock(*f.net, "game-cv-spins"));
}

TEST_CASE("A CALENDAR achievement waits for a synchronised clock", "[achievements]")
{
    auto definitions = json::parse(DEFINITIONS);
    definitions["calendar"] = {
            {"window=always",
             json::array({json::array({0, std::numeric_limits<int64_t>::max()})})}};
    definitions["results"].push_back({{"key", "game-cv-spins-always"},
                                      {"scope", "game"},
                                      {"is_single_session", false},
                                      {"frame", nullptr},
                                      {"frame_version", 0},
                                      {"rules", json::array({{{"type", "EVENT"},
                                                              {"comparison", ">="},
                                                              {"target", 100},
                                                              {"reference", "spins"}},
                                                             {{"type", "CALENDAR"},
                                                              {"comparison", ">="},
                                                              {"target", 1},
                                                              {"reference", "window=always"}}})}});
    Fixture f(json::array(), definitions.dump());
    const auto reportedKeys = [&f] {
        std::set<std::string> keys;
        for (const auto &report : f.net->reports) {
            for (const auto &item : report.body["achievements"]) {
                keys.insert(item["key"].get<std::string>());
            }
        }
        return keys;
    };

    f.net->clockSynchronised = false;
    f.game->addEvent("spins", 5);
    f.game->setCurrentBall(2);
    f.game->commit();
    CHECK(reportedKeys() == std::set<std::string> {"game-cv-spins"});
    f.net->reports.front().reply(ApiReply {Error::Success, 200, reply(json::array()), {}});
    f.net->reports.pop_front();
    f.game->runPendingPosts();

    f.net->clockSynchronised = true;
    f.game->addEvent("spins", 5);
    f.game->setCurrentBall(3);
    f.game->commit();
    CHECK(reportedKeys().contains("game-cv-spins-always"));
}

TEST_CASE("A baseline already achieved is not claimed again", "[achievements]")
{
    Fixture f(json::array({progressEntry("game-cv-boom", true, 0.0)}));

    f.game->addMode("balloon");
    f.game->commit();
    CHECK(f.net->reports.empty());
}

TEST_CASE("A failed report is retried with a fresh sequence", "[achievements]")
{
    Fixture f;
    f.game->addMode("balloon");
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1);

    f.net->reports.front().reply(ApiReply {Error::ApiError, 503, "busy"});
    f.net->reports.pop_front();
    f.game->runPendingPosts();
    CHECK(f.net->reports.empty());
    REQUIRE(f.net->retry);

    f.net->retry();
    f.game->runPendingPosts();
    REQUIRE(f.net->reports.size() == 1);
    CHECK(f.net->reports.front().body["sequence"] == 2);
    CHECK(f.net->reports.front().body["achievements"][0]["key"] == "game-cv-boom");
}

TEST_CASE("A claim whose reply was lost is granted by the retry's already_held", "[achievements]")
{
    // The server applied the first request, but its reply never arrived (§7.2)
    Fixture f;
    f.game->addMode("balloon");
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1);
    f.net->reports.front().reply(ApiReply {Error::ApiError, 0, ""});
    f.net->reports.pop_front();
    f.game->runPendingPosts();

    f.net->retry();
    f.game->runPendingPosts();
    REQUIRE(f.net->reports.size() == 1);
    f.net->reports.front().reply(outcome("game-cv-boom", "already_held"));
    f.game->runPendingPosts();
    REQUIRE(f.net->unlocks.size() == 1);
    CHECK(f.net->unlocks.front().key == "game-cv-boom");
}

TEST_CASE("A refused report is dropped, not retried", "[achievements]")
{
    Fixture f;
    f.game->addMode("balloon");
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1);

    f.net->reports.front().reply(ApiReply {
            Error::ApiError, 400,
            R"({"code":"not_claimed","detail":"The user is not claimed on that session."})"});
    f.net->reports.pop_front();
    f.game->runPendingPosts();
    CHECK_FALSE(f.net->retry);
    CHECK(f.net->reports.empty());
}

TEST_CASE("A rejected unlock is never raised", "[achievements]")
{
    Fixture f;
    f.game->addMode("balloon");
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1);

    f.net->reports.front().reply(
            ApiReply {Error::Success, 200,
                      reply({{{"key", "game-cv-boom"},
                              {"status", "rejected"},
                              {"code", "scope_mismatch"},
                              {"detail", "The achievement's scope does not permit it here."}}})});
    f.game->runPendingPosts();
    CHECK(f.net->unlocks.empty());
}

TEST_CASE("The end of the game flushes unlimited progress", "[achievements]")
{
    Fixture f;
    f.game->addEvent("spins", 3);
    f.game->commit();
    CHECK(f.net->reports.empty());

    f.game->setGameFinished();
    REQUIRE(f.net->reports.size() == 1);
    CHECK(f.net->reports.front().body["achievements"][0]["key"] == "game-cv-spins");
    CHECK(f.net->reports.front().body["achievements"][0]["rule_progress"][0]["value"] == 3);
}

TEST_CASE("A player claiming mid-game gets the facts of the whole session", "[achievements]")
{
    TempDir dir;
    auto fake = std::make_unique<FakeNet>(dir.str());
    auto *net = fake.get();
    GameStateImpl game(std::move(fake));
    net->definitionsReply(ApiReply {Error::Success, 200, DEFINITIONS});
    game.runPendingPosts();

    game.setGameStarted(GameStartOrigin::StartButton);
    net->sessionCreated("9ab2");
    game.runPendingPosts();

    game.addMode("balloon"); // before anyone claimed the slot
    game.commit();
    CHECK(net->reports.empty());

    net->claim(1, "3f1c");
    game.runPendingPosts();
    net->progressReply(ApiReply {Error::Success, 200, "[]"});
    game.runPendingPosts();

    REQUIRE(net->reports.size() == 1);
    CHECK(net->reports.front().body["user_id"] == "3f1c");
    CHECK(net->reports.front().body["achievements"][0]["key"] == "game-cv-boom");
}

TEST_CASE("An empty result is delivered, and acknowledges nothing", "[achievements]")
{
    // The API discards a report for a slot unclaimed this session (§7.3)
    Fixture f;
    f.game->addMode("balloon");
    f.game->addEvent("spins", 40);
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1); // the in_session claim

    f.net->reports.front().reply(ApiReply {Error::Success, 200, reply(json::array()), {}});
    f.net->reports.pop_front();
    f.game->runPendingPosts();
    CHECK(f.net->reports.empty()); // not retried
    CHECK_FALSE(f.net->retry);
    CHECK(f.net->unlocks.empty()); // nothing granted

    f.game->setCurrentBall(2);
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1);
    f.net->reports.front().reply(ApiReply {Error::Success, 200, reply(json::array()), {}});
    f.net->reports.pop_front();
    f.game->runPendingPosts();

    // Still unacknowledged, so the unchanged record rides the next ball's report
    f.game->setCurrentBall(3);
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1);
    CHECK(f.net->reports.front().body["achievements"][0]["key"] == "game-cv-spins");
}

TEST_CASE("A vacated slot's report is delivered under its player", "[achievements]")
{
    Fixture f;
    f.game->addMode("balloon");
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1); // in flight

    f.game->addEvent("spins", 7);
    f.game->commit();
    f.net->unclaim();
    f.game->runPendingPosts();
    f.game->setGameFinished(); // the slot is empty when the game ends

    // The queued report waits for the one in flight, then goes out for the departed player
    f.net->reports.front().reply(ApiReply {Error::Success, 200, reply(json::array()), {}});
    f.net->reports.pop_front();
    f.game->runPendingPosts();
    REQUIRE(f.net->reports.size() == 1);
    const auto &body = f.net->reports.front().body;
    CHECK(body["user_id"] == "3f1c");
    CHECK(body["achievements"][0]["key"] == "game-cv-spins");
    CHECK(body["achievements"][0]["rule_progress"][0]["value"] == 7);
}

TEST_CASE("A slot nobody claimed reports nothing", "[achievements]")
{
    Fixture f;
    f.game->setActivePlayer(2);
    f.game->addEvent("spins", 500);
    f.game->setCurrentBall(2);
    f.game->commit();
    f.game->setGameFinished();

    // Player 1 is claimed but did nothing; player 2's play has no one to be reported for
    CHECK(f.net->reports.empty());
}

TEST_CASE("A session uuid arriving after the game ended still releases its reports",
          "[achievements]")
{
    TempDir dir;
    auto fake = std::make_unique<FakeNet>(dir.str());
    auto *net = fake.get();
    GameStateImpl game(std::move(fake));
    net->definitionsReply(ApiReply {Error::Success, 200, DEFINITIONS});
    game.runPendingPosts();

    game.setGameStarted(GameStartOrigin::StartButton);
    auto firstCreated = net->sessionCreated;
    game.setGameFinished();

    game.setGameStarted(GameStartOrigin::StartButton);
    firstCreated("old-uuid"); // late reply for the first game
    game.runPendingPosts();
    CHECK(game.achievements().view()->players.empty()); // not bound to the running game
}

namespace {

/** Definitions of three achievements: two with a frame and an icon, one with neither. */
std::string artworkDefinitions(int64_t boomFrameVersion, const std::string &boomIcon,
                               bool withBlast)
{
    const auto achievement = [](const std::string &key, const std::string &mode) {
        return json {{"key", key},
                     {"scope", "game"},
                     {"is_single_session", true},
                     {"frame", nullptr},
                     {"frame_version", 0},
                     {"rules", json::array({{{"type", "MODE"},
                                             {"comparison", ">="},
                                             {"target", 1},
                                             {"reference", mode}}})}};
    };
    json results = json::array();
    auto boom = achievement("game-cv-boom", "balloon");
    boom["frame"] = "https://cdn.example/achievement_frame/game-cv-boom.png";
    boom["frame_version"] = boomFrameVersion;
    boom["icon"] = boomIcon;
    results.push_back(boom);
    if (withBlast) {
        auto blast = achievement("game-cv-blast", "blast");
        blast["frame"] = "https://cdn.example/achievement_frame/game-cv-blast.png?sig=a";
        blast["frame_version"] = 1;
        blast["icon"] = "https://cdn.example/icons/blast.gif";
        results.push_back(blast);
    }
    results.push_back(achievement("game-cv-plain", "plain"));
    return feed(results);
}

/** A game state given @p definitions, with the artwork of @p kind kept. */
struct ArtworkFixture {
    TempDir dir;
    FakeNet *net;
    std::unique_ptr<GameStateImpl> game;

    ArtworkFixture(sb_achievement_artwork_t kind, const std::string &definitions)
    {
        auto fake = std::make_unique<FakeNet>(dir.str(), kind);
        net = fake.get();
        game = std::make_unique<GameStateImpl>(std::move(fake));
        net->definitionsReply(ApiReply {Error::Success, 200, definitions, {}});
        game->runPendingPosts();
    }

    /** Answers the next artwork download with @p content, or fails it when empty. */
    void answer(const std::string &content)
    {
        REQUIRE_FALSE(net->artworks.empty());
        const auto download = net->artworks.front();
        net->artworks.pop_front();
        content.empty() ? download.fail() : download.complete(content);
        game->runPendingPosts();
    }

    std::string artworkOf(const std::string &key) const
    {
        const auto artwork = game->achievements().artwork(key);
        return artwork ? std::string(artwork->begin(), artwork->end()) : std::string {};
    }

    /** Starts a game with player 1 claimed, and unlocks game-cv-boom. */
    void unlockBoom()
    {
        game->setGameStarted(GameStartOrigin::StartButton);
        net->claim(1, "3f1c");
        net->sessionCreated("9ab2");
        game->runPendingPosts();
        net->progressReply(ApiReply {Error::Success, 200, "[]"});
        game->runPendingPosts();

        game->addMode("balloon");
        game->commit();
        REQUIRE(net->reports.size() == 1);
        net->reports.front().reply(outcome("game-cv-boom", "unlocked"));
        net->reports.pop_front();
        game->runPendingPosts();
    }
};

} // namespace

TEST_CASE("DMD frames are kept in memory and downloaded again only when changed", "[achievements]")
{
    ArtworkFixture f(SB_ACHIEVEMENT_ARTWORK_FRAME,
                     artworkDefinitions(4, "https://cdn.example/icons/boom.png", true));

    // One at a time, in definition order; achievements without a frame skipped
    REQUIRE(f.net->artworks.size() == 1);
    CHECK(f.net->artworks.front().url == "https://cdn.example/achievement_frame/game-cv-boom.png");
    f.answer("BOOM4");
    REQUIRE(f.net->artworks.size() == 1);
    CHECK(f.net->artworks.front().url
          == "https://cdn.example/achievement_frame/game-cv-blast.png?sig=a");
    f.answer("BLAST1");
    CHECK(f.net->artworks.empty());

    CHECK(f.artworkOf("game-cv-boom") == "BOOM4");
    CHECK(f.artworkOf("game-cv-blast") == "BLAST1");
    CHECK(f.artworkOf("game-cv-plain").empty());

    SECTION("Unchanged versions download nothing")
    {
        f.net->revalidate();
        f.game->runPendingPosts();
        f.net->definitionsReply(ApiReply {Error::Success, 304, {}, {}});
        f.game->runPendingPosts();
        CHECK(f.net->artworks.empty());
        CHECK(f.artworkOf("game-cv-boom") == "BOOM4");
    }

    SECTION("A bumped version is downloaded again; a dropped achievement loses its frame")
    {
        f.net->revalidate();
        f.game->runPendingPosts();
        f.net->definitionsReply(
                ApiReply {Error::Success,
                          200,
                          artworkDefinitions(5, "https://cdn.example/icons/boom.png", false),
                          {}});
        f.game->runPendingPosts();
        CHECK(f.artworkOf("game-cv-blast").empty());

        REQUIRE(f.net->artworks.size() == 1);
        CHECK(f.artworkOf("game-cv-boom") == "BOOM4"); // kept until the new one arrives
        f.answer("BOOM5");
        CHECK(f.artworkOf("game-cv-boom") == "BOOM5");
    }

    SECTION("A failed download is not retried until the next refresh")
    {
        f.net->revalidate();
        f.game->runPendingPosts();
        f.net->definitionsReply(
                ApiReply {Error::Success,
                          200,
                          artworkDefinitions(5, "https://cdn.example/icons/boom.png", true),
                          {}});
        f.game->runPendingPosts();

        f.answer({});
        CHECK(f.net->artworks.empty());
        CHECK(f.artworkOf("game-cv-boom") == "BOOM4");

        f.game->achievements().refreshArtwork();
        CHECK(f.net->artworks.size() == 1);
    }
}

TEST_CASE("Icons are fetched on the grant, not ahead", "[achievements]")
{
    ArtworkFixture f(SB_ACHIEVEMENT_ARTWORK_IMAGE,
                     artworkDefinitions(4, "https://cdn.example/icons/boom.png", true));
    CHECK(f.net->artworks.empty());

    SECTION("The unlock waits for its icon")
    {
        f.unlockBoom();
        CHECK(f.net->unlocks.empty());
        REQUIRE(f.net->artworks.size() == 1);
        CHECK(f.net->artworks.front().url == "https://cdn.example/icons/boom.png");

        f.answer("ICON1");
        REQUIRE(f.net->unlocks.size() == 1);
        CHECK(f.net->unlocks.front().artwork == "ICON1");
        CHECK(f.artworkOf("game-cv-boom") == "ICON1"); // kept for the next time

        // A changed icon drops the one kept, and nothing is fetched until it is asked for
        f.net->revalidate();
        f.game->runPendingPosts();
        f.net->definitionsReply(
                ApiReply {Error::Success,
                          200,
                          artworkDefinitions(4, "https://cdn.example/icons/boom-v2.png", true),
                          {}});
        f.game->runPendingPosts();
        CHECK(f.artworkOf("game-cv-boom").empty());
        CHECK(f.net->artworks.empty());
    }

    SECTION("An icon that can't be fetched leaves the unlock without it")
    {
        f.unlockBoom();
        f.answer({});
        REQUIRE(f.net->unlocks.size() == 1);
        CHECK(f.net->unlocks.front().artwork.empty());
    }
}

TEST_CASE("No artwork download starts while a report is in flight", "[achievements]")
{
    ArtworkFixture f(SB_ACHIEVEMENT_ARTWORK_FRAME,
                     artworkDefinitions(4, "https://cdn.example/icons/boom.png", true));
    REQUIRE(f.net->artworks.size() == 1); // boom's frame

    f.game->setGameStarted(GameStartOrigin::StartButton);
    f.net->claim(1, "3f1c");
    f.net->sessionCreated("9ab2");
    f.game->runPendingPosts();
    f.net->progressReply(ApiReply {Error::Success, 200, "[]"});
    f.game->runPendingPosts();
    f.game->addMode("balloon");
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1);

    f.answer("BOOM4");
    CHECK(f.net->artworks.empty()); // blast's frame waits for the report

    f.net->reports.front().reply(outcome("game-cv-boom", "unlocked"));
    f.net->reports.pop_front();
    f.game->runPendingPosts();
    REQUIRE(f.net->artworks.size() == 1);
    CHECK(f.net->artworks.front().url
          == "https://cdn.example/achievement_frame/game-cv-blast.png?sig=a");
}

namespace {

/** An icon store over a fake network, answering inline. */
struct IconStoreFixture {
    TempDir dir;
    FakeNet net {dir.str()};
    achievements::ArtworkStore store {net, [](std::function<void()> fn) { fn(); },
                                      achievements::ArtworkKind::Image};

    /** Asks for @p key's icon; the result lands in @p into once settled. */
    void ask(const std::string &key, std::optional<std::string> &into)
    {
        store.whenReady(key, "https://cdn.example/icons/" + key + ".png",
                        [&into](std::vector<uint8_t> data) {
                            into = std::string(data.begin(), data.end());
                        });
    }

    void answer(const std::string &content)
    {
        REQUIRE_FALSE(net.artworks.empty());
        const auto download = net.artworks.front();
        net.artworks.pop_front();
        download.complete(content);
    }

    bool holds(const std::string &key) const { return store.cached(key).has_value(); }
};

} // namespace

TEST_CASE("Icons asked for are fetched once, and the last eight kept", "[achievements]")
{
    IconStoreFixture f;

    SECTION("Two unlocks of one achievement share one download")
    {
        std::optional<std::string> first;
        std::optional<std::string> second;
        f.ask("a", first);
        f.ask("a", second);
        REQUIRE(f.net.artworks.size() == 1);
        f.answer("A");
        CHECK(first == "A");
        CHECK(second == "A");

        std::optional<std::string> third;
        f.ask("a", third);
        CHECK(f.net.artworks.empty());
        CHECK(third == "A");
    }

    SECTION("The least recently used icon is dropped first")
    {
        std::optional<std::string> result;
        for (int i = 0; i < static_cast<int>(achievements::ArtworkStore::ICON_CACHE_SIZE); ++i) {
            f.ask(std::to_string(i), result);
            f.answer("icon");
        }
        f.ask("0", result); // used again: now the most recent
        CHECK(f.net.artworks.empty());

        f.ask("new", result);
        f.answer("icon");
        CHECK(f.holds("0"));
        CHECK_FALSE(f.holds("1"));
        CHECK(f.holds("new"));
    }

    SECTION("Icons fetched ahead come last, and only a few wait unused")
    {
        std::vector<std::pair<std::string, std::string>> ahead;
        for (const auto *key : {"p1", "p2", "p3", "p4", "p5", "p6"}) {
            ahead.emplace_back(key, std::string("https://cdn.example/icons/") + key + ".png");
        }
        f.store.prefetch(ahead);
        REQUIRE(f.net.artworks.size() == 1);
        CHECK(f.net.artworks.front().url == "https://cdn.example/icons/p1.png");

        std::optional<std::string> asked;
        f.ask("a", asked); // jumps the queue once p1 is done
        f.answer("P1");
        REQUIRE(f.net.artworks.size() == 1);
        CHECK(f.net.artworks.front().url == "https://cdn.example/icons/a.png");
        f.answer("A");
        CHECK(asked == "A");

        while (!f.net.artworks.empty()) {
            f.answer("icon");
        }
        CHECK(f.holds("p4"));
        CHECK_FALSE(f.holds("p5"));

        // Each one handed over frees a place
        std::optional<std::string> used;
        f.ask("p1", used);
        CHECK(used == "P1");
        f.store.prefetch(ahead);
        REQUIRE(f.net.artworks.size() == 1);
        CHECK(f.net.artworks.front().url == "https://cdn.example/icons/p5.png");
    }

    SECTION("Nothing is fetched while a report is in flight")
    {
        f.store.setReportsInFlight(true);
        std::optional<std::string> result;
        f.ask("a", result);
        CHECK(f.net.artworks.empty());

        f.store.setReportsInFlight(false);
        REQUIRE(f.net.artworks.size() == 1);
        f.answer("A");
        CHECK(result == "A");
    }
}

TEST_CASE("The icon of an achievement about to be unlocked is fetched ahead", "[achievements]")
{
    json results = json::array();
    results.push_back({{"key", "game-cv-spinner"},
                       {"name", "Spinner"},
                       {"scope", "game"},
                       {"is_single_session", true},
                       {"icon", "https://cdn.example/icons/spinner.png"},
                       {"frame", nullptr},
                       {"frame_version", 0},
                       {"rules", json::array({{{"type", "EVENT"},
                                               {"comparison", ">="},
                                               {"target", 10},
                                               {"reference", "spins"}}})}});
    ArtworkFixture f(SB_ACHIEVEMENT_ARTWORK_IMAGE, feed(results));
    f.game->setGameStarted(GameStartOrigin::StartButton);
    f.net->claim(1, "3f1c");
    f.net->sessionCreated("9ab2");
    f.game->runPendingPosts();
    f.net->progressReply(ApiReply {Error::Success, 200, "[]"});
    f.game->runPendingPosts();

    f.game->addEvent("spins", 7);
    f.game->commit();
    CHECK(f.net->artworks.empty()); // not close yet

    f.game->addEvent("spins", 1);
    f.game->commit();
    REQUIRE(f.net->artworks.size() == 1);
    CHECK(f.net->artworks.front().url == "https://cdn.example/icons/spinner.png");
    f.answer("SPIN");

    // The grant finds the icon already held
    f.game->addEvent("spins", 2);
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1);
    f.net->reports.front().reply(outcome("game-cv-spinner", "unlocked"));
    f.net->reports.pop_front();
    f.game->runPendingPosts();
    CHECK(f.net->artworks.empty());
    REQUIRE(f.net->unlocks.size() == 1);
    CHECK(f.net->unlocks.front().artwork == "SPIN");
}

TEST_CASE("Without artwork chosen nothing is downloaded", "[achievements]")
{
    ArtworkFixture f(SB_ACHIEVEMENT_ARTWORK_NONE,
                     artworkDefinitions(4, "https://cdn.example/icons/boom.png", true));
    CHECK(f.net->artworks.empty());
}

TEST_CASE("An unlock carries its artwork, and waits for it", "[achievements]")
{
    ArtworkFixture f(SB_ACHIEVEMENT_ARTWORK_FRAME,
                     artworkDefinitions(4, "https://cdn.example/icons/boom.png", true));

    SECTION("Artwork already held is handed over with the unlock")
    {
        f.answer("BOOM4");
        f.answer("BLAST1");
        f.unlockBoom();
        REQUIRE(f.net->unlocks.size() == 1);
        CHECK(f.net->unlocks.front().artwork == "BOOM4");
    }

    SECTION("An unlock waits for its artwork's download")
    {
        f.unlockBoom();
        CHECK(f.net->unlocks.empty());

        f.answer("BOOM4");
        REQUIRE(f.net->unlocks.size() == 1);
        CHECK(f.net->unlocks.front().key == "game-cv-boom");
        CHECK(f.net->unlocks.front().artwork == "BOOM4");
    }

    SECTION("An unlock waits behind another achievement's download, too")
    {
        f.answer("BOOM4");
        f.net->revalidate();
        f.game->runPendingPosts();
        f.net->definitionsReply(
                ApiReply {Error::Success,
                          200,
                          artworkDefinitions(5, "https://cdn.example/icons/boom.png", true),
                          {}});
        f.game->runPendingPosts();
        // blast's first download is still in flight; boom's new version is queued behind it
        f.unlockBoom();
        CHECK(f.net->unlocks.empty());

        f.answer("BLAST1");
        CHECK(f.net->unlocks.empty());
        f.answer("BOOM5");
        REQUIRE(f.net->unlocks.size() == 1);
        CHECK(f.net->unlocks.front().artwork == "BOOM5");
    }

    SECTION("An unlock whose artwork can't be downloaded is raised without it")
    {
        f.unlockBoom();
        f.answer({});
        REQUIRE(f.net->unlocks.size() == 1);
        CHECK(f.net->unlocks.front().artwork.empty());
    }
}

TEST_CASE("One unlock event is raised per grant", "[achievements]")
{
    TempDir dir;
    FakeNet net {dir.str()};
    achievements::ArtworkStore artwork {net, [](std::function<void()> fn) { fn(); },
                                        achievements::ArtworkKind::None};
    std::vector<achievements::AchievementUnlock> raised;
    achievements::UnlockPresenter presenter {
            artwork, [&](achievements::AchievementUnlock unlock) { raised.push_back(unlock); }};

    const achievements::GrantId grant {1, "3f1c", "game-cv-top-spinner"};
    const auto unlock = [](sb_player_t player) {
        return achievements::AchievementUnlock {"game-cv-top-spinner", player, "Top", "", {}};
    };

    presenter.granted(grant, unlock(1), {});
    presenter.granted(grant, unlock(1), {}); // the same grant, signalled twice
    CHECK(raised.size() == 1);

    SECTION("A grant revoked and granted again is a new grant")
    {
        presenter.revoked(grant);
        presenter.granted(grant, unlock(1), {});
        CHECK(raised.size() == 2);
    }

    SECTION("Grants of other players and sessions are separate")
    {
        presenter.granted({1, "77aa", grant.key}, unlock(2), {});
        presenter.granted({2, grant.userId, grant.key}, unlock(1), {});
        CHECK(raised.size() == 3);
    }

    SECTION("A session forgotten keeps nothing")
    {
        presenter.forgetSession(1);
        presenter.granted(grant, unlock(1), {});
        CHECK(raised.size() == 2);
    }
}

TEST_CASE("Definitions are revalidated at least daily, and after local midnight", "[achievements]")
{
    using namespace std::chrono;
    using namespace scorbit::detail::achievements;
    constexpr int64_t HOUR = 3'600'000;
    const int64_t noon = 1793448000000; // 2026-10-31 12:00 UTC

    TempDir dir;
    FakeNet net {dir.str()};
    int64_t now = noon;
    AchievementService service {net,
                                AchievementStorage {dir.str()},
                                [](std::function<void()> fn) { fn(); },
                                {},
                                ArtworkKind::None,
                                [&now] { return now; }};

    SECTION("Without a time zone, after UTC midnight, spread over a few minutes")
    {
        service.refreshDefinitions();
        net.definitionsReply(ApiReply {Error::Success, 200, DEFINITIONS, {}});
        CHECK(net.revalidationDelay >= milliseconds {12 * HOUR});
        CHECK(net.revalidationDelay <= milliseconds {12 * HOUR} + 10min);

        REQUIRE(net.revalidate);
        net.revalidate();
        CHECK(net.definitionsFetches == 2);

        // A 304 confirms them just the same
        now += 12 * HOUR;
        net.definitionsReply(ApiReply {Error::Success, 304, {}, {}});
        CHECK(net.revalidationDelay >= milliseconds {24 * HOUR} - 1ms);
        CHECK(net.revalidationDelay <= milliseconds {24 * HOUR});
    }

    SECTION("After the venue's midnight")
    {
        auto definitions = json::parse(DEFINITIONS);
        definitions["utc_offsets"] = json::array({json::array({1767225600000, -18000})}); // UTC-5
        service.refreshDefinitions();
        net.definitionsReply(ApiReply {Error::Success, 200, definitions.dump(), {}});
        CHECK(net.revalidationDelay >= milliseconds {17 * HOUR});
        CHECK(net.revalidationDelay <= milliseconds {17 * HOUR} + 10min);
    }

    SECTION("A revalidation missed while offline happens at the next game start")
    {
        service.refreshDefinitions();
        net.definitionsReply(ApiReply {Error::Success, 200, DEFINITIONS, {}});

        now += 13 * HOUR; // past midnight, and the timer never fired
        service.onSessionStarted(1, TimelineRow {now, 1, 1, {}, {}, {}, {}, true});
        CHECK(net.definitionsFetches == 2);
    }
}

TEST_CASE("Definitions are refetched on token refresh, not at game start", "[achievements]")
{
    TempDir dir;
    auto fake = std::make_unique<FakeNet>(dir.str());
    auto *net = fake.get();
    GameStateImpl game(std::move(fake));

    // The boot fetch never succeeded, e.g. the machine was not paired yet
    net->definitionsReply(ApiReply {Error::NotPaired, 0, {}});
    game.runPendingPosts();
    net->definitionsReply = {};

    game.setGameStarted(GameStartOrigin::StartButton);
    game.setGameFinished();
    CHECK(net->definitionsFetches == 1);

    REQUIRE(net->tokenRefreshed);
    net->tokenRefreshed();
    game.runPendingPosts();
    CHECK(net->definitionsFetches == 2);
    REQUIRE(net->definitionsReply);
    net->definitionsReply(ApiReply {Error::Success, 200, DEFINITIONS});
    game.runPendingPosts();
    CHECK(game.achievements().view()->definitions->size() == 2);
}

TEST_CASE("Definitions fetched during a game apply from the next game", "[achievements]")
{
    Fixture f;

    auto refreshed = json::parse(DEFINITIONS);
    refreshed["results"].push_back({{"key", "game-cv-blast"},
                                    {"scope", "game"},
                                    {"is_single_session", true},
                                    {"frame", nullptr},
                                    {"frame_version", 0},
                                    {"rules", json::array({{{"type", "MODE"},
                                                            {"comparison", ">="},
                                                            {"target", 1},
                                                            {"reference", "blast"}}})}});
    f.net->tokenRefreshed();
    f.game->runPendingPosts();
    REQUIRE(f.net->definitionsReply);
    f.net->definitionsReply(ApiReply {Error::Success, 200, refreshed.dump()});
    f.game->runPendingPosts();
    CHECK(f.game->achievements().view()->definitions->size() == 2);

    f.game->addMode("blast");
    f.game->commit();
    CHECK_FALSE(reported(*f.net, "game-cv-blast"));

    f.game->setGameFinished();
    CHECK(f.game->achievements().view()->definitions->size() == 3);

    f.game->setGameStarted(GameStartOrigin::StartButton);
    f.net->sessionCreated("9ab3");
    f.game->runPendingPosts();
    f.net->progressReply(ApiReply {Error::Success, 200, "[]"});
    f.game->runPendingPosts();

    f.game->addMode("blast");
    f.game->commit();
    CHECK(reported(*f.net, "game-cv-blast"));
}
