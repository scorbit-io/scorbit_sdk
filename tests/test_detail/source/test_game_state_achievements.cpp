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
#include "net_base.h"
#include "event_classes.h"

#include <catch2/catch_test_macros.hpp>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <deque>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using json = nlohmann::json;

namespace {

/** A NetBase whose achievements requests are answered by the test. */
class FakeNet : public NetBase
{
public:
    explicit FakeNet(std::string dataDir) { m_info.dataDir = std::move(dataDir); }

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

    void fetchAchievementDefinitions(std::string etag, ApiReplyCallback cb) override
    {
        definitionsEtag = std::move(etag);
        definitionsReply = std::move(cb);
    }
    void fetchAchievementProgress(std::string userId, ApiReplyCallback cb) override
    {
        progressUserId = std::move(userId);
        progressReply = std::move(cb);
    }
    void postAchievementReport(std::string body, ApiReplyCallback cb) override
    {
        reports.push_back({json::parse(body), std::move(cb)});
    }
    void scheduleAchievementRetry(std::chrono::steady_clock::duration,
                                  std::function<void()> fn) override
    {
        retry = std::move(fn);
    }
    void setPlayersChangedCallback(PlayersChangedCallback cb) override
    {
        playersChanged = std::move(cb);
    }
    void publishEvent(EventPtr event) override
    {
        if (auto *e = dynamic_cast<AchievementUpdatedEvent *>(event.get())) {
            updates.push_back({e->key(), e->player(), e->status()});
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

    struct Report {
        json body;
        ApiReplyCallback reply;
    };
    struct Update {
        std::string key;
        sb_player_t player;
        sb_achievement_status_t status;
    };

    SessionCreatedCallback sessionCreated;
    std::string definitionsEtag;
    ApiReplyCallback definitionsReply;
    std::string progressUserId;
    ApiReplyCallback progressReply;
    std::deque<Report> reports;
    std::function<void()> retry;
    PlayersChangedCallback playersChanged;
    std::vector<Update> updates;
    int rows {0};

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
  "results": [
    {"key": "game-cv-boom", "scope": "game", "evaluation": "in_session",
     "rules": [{"type": "MODE", "comparison": "GE", "target": 1, "reference": "balloon"}]},
    {"key": "game-cv-spins", "scope": "game", "evaluation": "unlimited",
     "rules": [{"type": "EVENT", "comparison": "GE", "target": 100, "reference": "spins"}]}
  ]})";

std::string reply(const json &results)
{
    return json {{"results", results}}.dump();
}

bool hasUpdate(const FakeNet &net, const std::string &key, sb_achievement_status_t status)
{
    return std::ranges::any_of(net.updates, [&](const FakeNet::Update &u) {
        return u.key == key && u.status == status;
    });
}

/** A game state with definitions loaded, a started session and player 1 claimed. */
struct Fixture {
    TempDir dir;
    FakeNet *net;
    std::unique_ptr<GameStateImpl> game;

    explicit Fixture(json baseline = json::array())
    {
        auto fake = std::make_unique<FakeNet>(dir.str());
        net = fake.get();
        game = std::make_unique<GameStateImpl>(std::move(fake));

        REQUIRE(net->definitionsReply);
        net->definitionsReply(ApiReply {Error::Success, 200, DEFINITIONS, "\"v1\""});
        game->runPendingPosts();

        game->setGameStarted(GameStartOrigin::StartButton);
        REQUIRE(net->sessionCreated);
        net->claim(1, "3f1c");
        net->sessionCreated("9ab2");
        game->runPendingPosts();

        REQUIRE(net->progressUserId == "3f1c");
        net->progressReply(ApiReply {
                Error::Success, 200, json {{"user_id", "3f1c"}, {"results", baseline}}.dump(), {}});
        game->runPendingPosts();
    }
};

} // namespace

TEST_CASE("Definitions are cached and revalidated with their ETag", "[achievements]")
{
    TempDir dir;
    {
        auto fake = std::make_unique<FakeNet>(dir.str());
        auto *net = fake.get();
        GameStateImpl game(std::move(fake));
        net->definitionsReply(ApiReply {Error::Success, 200, DEFINITIONS, "\"v1\""});
        game.runPendingPosts();
        CHECK(game.achievements().view()->definitions->size() == 2);
    }

    // Next boot: the cache is loaded before the network answers, and revalidated with the ETag
    auto fake = std::make_unique<FakeNet>(dir.str());
    auto *net = fake.get();
    GameStateImpl game(std::move(fake));
    CHECK(net->definitionsEtag == "\"v1\"");
    CHECK(game.achievements().view()->definitions->size() == 2);

    net->definitionsReply(ApiReply {Error::Success, 304, {}, {}});
    game.runPendingPosts();
    CHECK(game.achievements().view()->definitions->size() == 2);
}

TEST_CASE("An in_session unlock is decided on commit and reported at once", "[achievements]")
{
    Fixture f;

    f.game->addMode("balloon");
    f.game->commit();

    CHECK(hasUpdate(*f.net, "game-cv-boom", SB_ACHIEVEMENT_UNLOCKED_LOCALLY));
    REQUIRE(f.net->reports.size() == 1);
    const auto &body = f.net->reports.front().body;
    CHECK(body["user_id"] == "3f1c");
    CHECK(body["session_uuid"] == "9ab2");
    CHECK(body["sequence"] == 1);
    REQUIRE(body["achievements"].size() == 1);
    CHECK(body["achievements"][0]["key"] == "game-cv-boom");
    CHECK(body["achievements"][0]["achieved"] == true);

    f.net->reports.front().reply(ApiReply {
            Error::Success, 200, reply({{{"key", "game-cv-boom"}, {"status", "unlocked"}}}), {}});
    f.game->runPendingPosts();
    CHECK(hasUpdate(*f.net, "game-cv-boom", SB_ACHIEVEMENT_CONFIRMED));
}

TEST_CASE("Unlimited progress is reported at the ball boundary", "[achievements]")
{
    Fixture f(json::array(
            {{{"key", "game-cv-spins"},
              {"achieved", false},
              {"rule_progress", {{{"index", 0}, {"value", 90}, {"satisfied", false}}}}}}));

    f.game->addEvent("spins", 4);
    f.game->commit();
    CHECK(f.net->reports.empty()); // not streamed

    f.game->addEvent("spins", 6);
    f.game->commit();
    CHECK(hasUpdate(*f.net, "game-cv-spins", SB_ACHIEVEMENT_UNLOCKED_LOCALLY));
    CHECK(f.net->reports.empty());

    f.game->setCurrentBall(2);
    f.game->commit();

    REQUIRE(f.net->reports.size() == 1);
    const auto &item = f.net->reports.front().body["achievements"][0];
    CHECK(item["key"] == "game-cv-spins");
    CHECK(item["achieved"] == true);
    CHECK(item["rule_progress"][0]["value"] == 100); // lifetime: 90 + 4 + 6
}

TEST_CASE("A failed report is retried with a fresh sequence", "[achievements]")
{
    Fixture f;
    f.game->addMode("balloon");
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1);

    f.net->reports.front().reply(ApiReply {Error::ApiError, 503, "busy", {}});
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

TEST_CASE("A rejected unlock is retracted", "[achievements]")
{
    Fixture f;
    f.game->addMode("balloon");
    f.game->commit();
    REQUIRE(f.net->reports.size() == 1);

    f.net->reports.front().reply(ApiReply {
            Error::Success,
            200,
            reply({{{"key", "game-cv-boom"}, {"status", "rejected"}, {"code", "scope_mismatch"}}}),
            {}});
    f.game->runPendingPosts();
    CHECK(hasUpdate(*f.net, "game-cv-boom", SB_ACHIEVEMENT_RETRACTED));
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
    net->definitionsReply(ApiReply {Error::Success, 200, DEFINITIONS, {}});
    game.runPendingPosts();

    game.setGameStarted(GameStartOrigin::StartButton);
    net->sessionCreated("9ab2");
    game.runPendingPosts();

    game.addMode("balloon"); // before anyone claimed the slot
    game.commit();
    CHECK(net->reports.empty());

    net->claim(1, "3f1c");
    game.runPendingPosts();
    net->progressReply(ApiReply {Error::Success, 200, reply(json::array()), {}});
    game.runPendingPosts();

    CHECK(hasUpdate(*net, "game-cv-boom", SB_ACHIEVEMENT_UNLOCKED_LOCALLY));
    REQUIRE(net->reports.size() == 1);
    CHECK(net->reports.front().body["user_id"] == "3f1c");
}

TEST_CASE("A session uuid arriving after the game ended still releases its reports",
          "[achievements]")
{
    TempDir dir;
    auto fake = std::make_unique<FakeNet>(dir.str());
    auto *net = fake.get();
    GameStateImpl game(std::move(fake));
    net->definitionsReply(ApiReply {Error::Success, 200, DEFINITIONS, {}});
    game.runPendingPosts();

    game.setGameStarted(GameStartOrigin::StartButton);
    auto firstCreated = net->sessionCreated;
    game.setGameFinished();

    game.setGameStarted(GameStartOrigin::StartButton);
    firstCreated("old-uuid"); // late reply for the first game
    game.runPendingPosts();
    CHECK(game.achievements().view()->players.empty()); // not bound to the running game
}
