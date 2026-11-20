/*
 * Scorbit SDK
 *
 * (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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
#include "net.h"
#include "competitions/competition_service.h"
#include "competitions/message_outbox.h"
#include "competitions/messages.h"
#include "net_base.h"
#include "event_classes.h"

#include <scorbit_sdk/scorbit_sdk.h>
#include <catch2/catch_test_macros.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/filesystem.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <deque>
#include <fstream>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using namespace scorbit::detail::competitions;
using json = nlohmann::json;

namespace {

// The API's examples, as in api core/contracts/competition/v1/ on
// feature/SB-6365-reserve-attempt-game-start.

const char *GOAL = R"({
  "version": 1,
  "tournament": "0193a5e0-0000-7000-8000-000000000001",
  "rules": "0193a5e0-0000-7000-8000-000000000002",
  "checksum": "ca5f30ab82d2a8d3a69f1041795a15f3f5e78abea9239f272793fd1253ee4d53",
  "attempt": "0193a5e0-0000-7000-8000-000000000003",
  "type": "competition.goal",
  "player": "6f1c2d3e-4a5b-4c6d-8e7f-000000000004",
  "venuemachine": "6f1c2d3e-4a5b-4c6d-8e7f-000000000005",
  "position": null,
  "definition": {
    "format_version": 1,
    "captured_at": "2026-11-06T18:00:00Z",
    "evaluator_version": 1,
    "timer_policy": {"name": "current_player_turn", "version": 1},
    "achievement": {
      "id": 115, "uuid": "0193a5e0-0000-7000-8000-0000000000aa",
      "key": "game-totan-achievement-tournament", "name": "Ali Baba Express",
      "description": "Play the Tale of Ali Baba and score.", "is_single_session": true,
      "scope": "game", "game": "totan", "venue": null, "variants": [], "event": null
    },
    "rules": [
      {"type": "MODE", "comparison": ">=", "target": 1, "reference": "NA:Tale of Ali Baba"},
      {"type": "SCORE", "comparison": ">=", "target": 1, "reference": null}
    ],
    "calendar": null
  }
})";

const char *GOAL_INSTALLED = R"({
  "version": 1,
  "tournament": "0193a5e0-0000-7000-8000-000000000001",
  "rules": "0193a5e0-0000-7000-8000-000000000002",
  "checksum": "ca5f30ab82d2a8d3a69f1041795a15f3f5e78abea9239f272793fd1253ee4d53",
  "attempt": "0193a5e0-0000-7000-8000-000000000003",
  "type": "competition.goal_installed",
  "installed_at": "2026-11-06T19:59:58.120Z"
})";

const char *ATTEMPT_STARTED = R"({
  "version": 1,
  "tournament": "0193a5e0-0000-7000-8000-000000000001",
  "rules": "0193a5e0-0000-7000-8000-000000000002",
  "checksum": "ca5f30ab82d2a8d3a69f1041795a15f3f5e78abea9239f272793fd1253ee4d53",
  "attempt": "0193a5e0-0000-7000-8000-000000000003",
  "type": "competition.attempt_started",
  "session": "6f1c2d3e-4a5b-4c6d-8e7f-000000000006",
  "position": 1,
  "started_at": "2026-11-06T20:00:00.250Z",
  "boot_id": "b7e1c0de-5a1e-4c00-9d00-000000000001",
  "sequence": 1041
})";

const char *GOAL_INSTALLED_SAVED = R"({
  "version": 1,
  "tournament": "0193a5e0-0000-7000-8000-000000000001",
  "rules": "0193a5e0-0000-7000-8000-000000000002",
  "checksum": "ca5f30ab82d2a8d3a69f1041795a15f3f5e78abea9239f272793fd1253ee4d53",
  "attempt": "0193a5e0-0000-7000-8000-000000000003",
  "type": "competition.goal_installed_saved",
  "status": "saved",
  "received_at": "2026-11-06T19:59:58.402000+00:00"
})";

const char *ATTEMPT_STARTED_SAVED = R"({
  "version": 1,
  "tournament": "0193a5e0-0000-7000-8000-000000000001",
  "rules": "0193a5e0-0000-7000-8000-000000000002",
  "checksum": "ca5f30ab82d2a8d3a69f1041795a15f3f5e78abea9239f272793fd1253ee4d53",
  "attempt": "0193a5e0-0000-7000-8000-000000000003",
  "type": "competition.attempt_started_saved",
  "status": "saved",
  "received_at": "2026-11-06T20:00:00.731000+00:00"
})";

const char *ATTEMPT_REFUSED = R"({
  "code": "slot_mismatch",
  "detail": "This attempt already started on another game or position."
})";

// The release messages (SB-6368), as in api core/contracts/competition/v1/ on main.

const char *ATTEMPT_CONTROL = R"({
  "version": 1,
  "tournament": "0193a5e0-0000-7000-8000-000000000001",
  "rules": "0193a5e0-0000-7000-8000-000000000002",
  "checksum": "ca5f30ab82d2a8d3a69f1041795a15f3f5e78abea9239f272793fd1253ee4d53",
  "attempt": "0193a5e0-0000-7000-8000-000000000003",
  "type": "competition.attempt_control",
  "action": "release",
  "control": "0193a5e0-0000-7000-8000-000000000009",
  "end_reason": "released"
})";

const char *ATTEMPT_RELEASED = R"({
  "version": 1,
  "tournament": "0193a5e0-0000-7000-8000-000000000001",
  "rules": "0193a5e0-0000-7000-8000-000000000002",
  "checksum": "ca5f30ab82d2a8d3a69f1041795a15f3f5e78abea9239f272793fd1253ee4d53",
  "attempt": "0193a5e0-0000-7000-8000-000000000003",
  "type": "competition.attempt_released",
  "control": "0193a5e0-0000-7000-8000-000000000009",
  "state": "released",
  "boot_id": "b7e1c0de-5a1e-4c00-9d00-000000000001",
  "sequence": 1077,
  "qualification": null
})";

const char *ATTEMPT_RELEASED_SAVED = R"({
  "version": 1,
  "tournament": "0193a5e0-0000-7000-8000-000000000001",
  "rules": "0193a5e0-0000-7000-8000-000000000002",
  "checksum": "ca5f30ab82d2a8d3a69f1041795a15f3f5e78abea9239f272793fd1253ee4d53",
  "attempt": "0193a5e0-0000-7000-8000-000000000003",
  "type": "competition.attempt_released_saved",
  "control": "0193a5e0-0000-7000-8000-000000000009",
  "status": "saved",
  "refunded": true,
  "received_at": "2026-11-06T20:04:10.112000+00:00"
})";

const char *RELEASE_PENDING = R"({
  "code": "release_pending",
  "detail": "This attempt's release hasn't been acknowledged yet."
})";

constexpr auto CONTROL = "0193a5e0-0000-7000-8000-000000000009";

constexpr auto TOURNAMENT = "0193a5e0-0000-7000-8000-000000000001";
constexpr auto ATTEMPT = "0193a5e0-0000-7000-8000-000000000003";
constexpr auto PLAYER = "6f1c2d3e-4a5b-4c6d-8e7f-000000000004";
constexpr auto SESSION = "6f1c2d3e-4a5b-4c6d-8e7f-000000000006";

/** 2026-11-06T20:00:00.250Z */
constexpr int64_t STARTED_AT_MS = 1'793'995'200'250;

json goal()
{
    return json::parse(GOAL);
}

competitions::CompetitionGoal parsed(const json &message = goal())
{
    std::string error;
    auto rv = parseGoal(message, error);
    REQUIRE(rv);
    return *rv;
}

class TempDir
{
public:
    TempDir()
        : m_path {boost::filesystem::temp_directory_path()
                  / boost::filesystem::unique_path("sb-comp-%%%%-%%%%")}
    {
    }
    ~TempDir() { boost::filesystem::remove_all(m_path); }
    std::string str() const { return m_path.string(); }

private:
    boost::filesystem::path m_path;
};

/** A NetBase whose competition messages are answered by the test. */
class FakeNet : public NetBase
{
public:
    explicit FakeNet(std::string dataDir)
        : boot {fmt::format("6c0e9d1a-2f3b-4c5d-8e9f-{:012}", ++boots)}
    {
        m_info.dataDir = std::move(dataDir);
    }

    AuthStatus status() const override { return auth; }
    void authenticate() override { }
    void updateConfig(const std::string &, const std::string &, bool, std::optional<std::string>,
                      HttpStatusCallback) override
    {
    }
    void sessionCreate(const GameData &, GameStartOrigin, SessionCreatedCallback cb) override
    {
        sessionCreated = std::move(cb);
    }
    void submitGameData(const GameData &, SessionFlags) override { }
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

    void setSessionPlayersCallback(SessionPlayersCallback cb) override
    {
        sessionPlayers = std::move(cb);
    }
    void setCompetitionGoalCallback(CompetitionGoalCallback cb) override
    {
        goalReceived = std::move(cb);
    }
    void setAttemptReleaseCallback(AttemptReleaseCallback cb) override
    {
        releaseReceived = std::move(cb);
    }
    bool isClockSynchronised() const override { return clockSynchronised; }
    const std::string &bootId() const override { return boot; }
    std::string venueMachine() const override { return venue; }
    void postCompetitionMessage(MessageKind kind, std::string body, ApiReplyCallback cb) override
    {
        sent.push_back({kind, json::parse(body), std::move(cb)});
    }
    void scheduleCompetitionRetry(std::chrono::steady_clock::duration delay,
                                  std::function<void()> fn) override
    {
        retryDelay = delay;
        retry = std::move(fn);
    }
    void fetchAchievementDefinitions(std::string, ApiReplyCallback cb) override
    {
        definitionsReply = std::move(cb);
    }
    void fetchAchievementProgress(std::string userId, ApiReplyCallback cb) override
    {
        progressUserId = std::move(userId);
        progressReply = std::move(cb);
    }
    void postAchievementReport(std::string body, ApiReplyCallback) override
    {
        reports.push_back(json::parse(body));
    }
    void setPlayersChangedCallback(PlayersChangedCallback cb) override
    {
        playersChanged = std::move(cb);
    }

    /** The shared profiles seat @p userId at @p position, as ordinary achievements see it. */
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

    void publishEvent(EventPtr event) override
    {
        if (auto *e = dynamic_cast<AchievementUnlockedEvent *>(event.get())) {
            unlocks.push_back(e->key());
        }
        if (auto *e = dynamic_cast<CompetitionGoalReceivedEvent *>(event.get())) {
            goals.push_back({e->attempt(), e->player(), e->position(), e->name()});
        }
    }

    /** A reply for @p session seats @p userId at @p position, its Entry for @p tournament. */
    void seat(const std::string &session, sb_player_t position, const std::string &userId,
              const std::string &tournament = TOURNAMENT)
    {
        sessionPlayers(session, {{position, SessionSeat {userId, tournament}}});
    }

    struct Sent {
        MessageKind kind;
        json body;
        ApiReplyCallback reply;
    };

    /** Each Net's own, as Net generates one per instance. */
    static inline int boots {0};
    std::string boot;
    AuthStatus auth {AuthStatus::AuthenticatedPaired};

    SessionCreatedCallback sessionCreated;
    SessionPlayersCallback sessionPlayers;
    bool clockSynchronised {true};
    std::string venue {"6f1c2d3e-4a5b-4c6d-8e7f-000000000005"};
    CompetitionGoalCallback goalReceived;
    AttemptReleaseCallback releaseReceived;
    std::deque<Sent> sent;
    std::function<void()> retry;
    std::chrono::steady_clock::duration retryDelay {};
    std::vector<scorbit::CompetitionGoal> goals;
    ApiReplyCallback definitionsReply;
    ApiReplyCallback progressReply;
    std::string progressUserId;
    PlayersChangedCallback playersChanged;
    std::vector<json> reports;
    std::vector<std::string> unlocks;

private:
    DeviceInfo m_info;
    PlayerProfilesManager m_players;
    std::string m_empty;
};

ApiReply receipt(const char *body)
{
    return ApiReply {Error::Success, 200, body, {}};
}

ApiReply refused(const char *body)
{
    return ApiReply {Error::ApiError, 409, body, {}};
}

ApiReply noAnswer()
{
    return ApiReply {Error::ApiError, 0, {}, {}};
}

/** A game state on a fresh data dir, with its network faked. */
struct Fixture {
    TempDir dir;
    FakeNet *net {nullptr};
    std::unique_ptr<GameStateImpl> game;

    Fixture() { boot(); }

    /** (Re)starts game state on the same data dir, as after a restart. */
    void boot()
    {
        game.reset();
        auto fake = std::make_unique<FakeNet>(dir.str());
        net = fake.get();
        game = std::make_unique<GameStateImpl>(std::move(fake));
        run();
    }

    void run() { game->runPendingPosts(); }

    void deliver(const json &message = goal())
    {
        std::string error;
        auto parsedGoal = parseGoal(message, error);
        REQUIRE(parsedGoal);
        net->goalReceived(std::move(*parsedGoal));
        run();
    }

    /** The API's release of the example attempt arrives, as Net hands it over. */
    void release(const json &message = json::parse(ATTEMPT_CONTROL))
    {
        std::string error;
        auto parsedRelease = parseAttemptControl(message, error);
        REQUIRE(parsedRelease);
        net->releaseReceived(std::move(*parsedRelease));
        run();
    }

    /** Lets the backoff elapse, so the next message goes out. */
    void retry()
    {
        REQUIRE(net->retry);
        net->retry();
        run();
    }

    /** Answers the oldest message sent and lets its effects run. */
    void answer(ApiReply reply)
    {
        REQUIRE_FALSE(net->sent.empty());
        auto sent = std::move(net->sent.front());
        net->sent.pop_front();
        sent.reply(std::move(reply));
        run();
    }

    /** Installs the example goal and has the API acknowledge it. */
    void install()
    {
        deliver();
        REQUIRE(net->sent.size() == 1);
        answer(receipt(GOAL_INSTALLED_SAVED));
    }

    void startGame()
    {
        game->setGameStarted(GameStartOrigin::StartButton);
        run();
    }

    /**
     * The server created the latest game's session, its reply seating @p userId at @p position,
     * as Net delivers it: the seats under the session's uuid, then the uuid. The first game's
     * session is SESSION, each later one another. Returns the uuid.
     */
    std::string sessionCreated(const std::string &userId = PLAYER, sb_player_t position = 1,
                               const std::string &tournament = TOURNAMENT)
    {
        REQUIRE(net->sessionCreated);
        const auto session = sessions++ == 0
                                     ? std::string {SESSION}
                                     : fmt::format("6f1c2d3e-4a5b-4c6d-8e7f-0000000001{:02}",
                                                   sessions);
        net->seat(session, position, userId, tournament);
        net->sessionCreated(session);
        run();
        return session;
    }

    int sessions {0};

    const AttemptRecord &record() const { return game->competitions().attempts().at(ATTEMPT); }
};

} // namespace

// ---- Messages ----------------------------------------------------------------------------------

TEST_CASE("A competition goal parses as the API sends it", "[competitions]")
{
    const auto parsedGoal = parsed();
    CHECK(parsedGoal.identity.tournament == "0193a5e0-0000-7000-8000-000000000001");
    CHECK(parsedGoal.identity.rules == "0193a5e0-0000-7000-8000-000000000002");
    CHECK(parsedGoal.identity.checksum
          == "ca5f30ab82d2a8d3a69f1041795a15f3f5e78abea9239f272793fd1253ee4d53");
    CHECK(parsedGoal.identity.attempt == ATTEMPT);
    CHECK(parsedGoal.player == PLAYER);
    CHECK_FALSE(parsedGoal.position);
    CHECK(parsedGoal.name == "Ali Baba Express");
    CHECK(parsedGoal.definition == goal()["definition"]);

    auto withPosition = goal();
    withPosition["position"] = 2;
    CHECK(parsed(withPosition).position == 2);

    // The name is only shown: a goal without one is still installed
    auto withoutName = goal();
    withoutName["definition"]["achievement"].erase("name");
    CHECK(parsed(withoutName).name.empty());
}

TEST_CASE("A competition goal the SDK can't fully judge is refused whole", "[competitions]")
{
    auto refusedFor = [](const std::function<void(json &)> &change) {
        auto message = goal();
        change(message);
        std::string error;
        const bool refused = !parseGoal(message, error);
        CHECK_FALSE(error.empty());
        return refused;
    };

    CHECK(refusedFor([](json &m) { m["version"] = 2; }));
    CHECK(refusedFor([](json &m) { m["type"] = "competition.attempt_control"; }));
    CHECK(refusedFor([](json &m) { m["definition"]["format_version"] = 2; }));
    CHECK(refusedFor([](json &m) { m["definition"]["evaluator_version"] = 2; }));
    CHECK(refusedFor([](json &m) { m["definition"]["timer_policy"]["name"] = "whole_game"; }));
    CHECK(refusedFor([](json &m) { m["definition"]["timer_policy"]["version"] = 2; }));
    CHECK(refusedFor([](json &m) { m["definition"]["rules"] = json::array(); }));
    // One unknown rule refuses the goal: it is never installed with the rule skipped
    CHECK(refusedFor([](json &m) {
        m["definition"]["rules"].push_back(
                {{"type", "TELEPORT"}, {"comparison", ">="}, {"target", 1}, {"reference", ""}});
    }));
    // Known to the SDK, but not judged within one game
    CHECK(refusedFor([](json &m) { m["definition"]["rules"][0]["type"] = "SESSION"; }));
    CHECK(refusedFor([](json &m) { m["definition"]["rules"][0]["type"] = "ACHIEVEMENT"; }));
    CHECK(refusedFor([](json &m) { m["definition"]["rules"][0]["comparison"] = "~"; }));
    // Parses, but the evaluator can't judge it: a MODE rule naming no mode
    CHECK(refusedFor([](json &m) { m["definition"]["rules"][0]["reference"] = nullptr; }));
    {
        auto message = goal();
        message["definition"]["rules"][0]["reference"] = nullptr;
        std::string error;
        CHECK_FALSE(parseGoal(message, error));
        CHECK(error.find("can't be evaluated") != std::string::npos);
    }
    // A CALENDAR window on the venue's clock, with no offsets to place it
    CHECK(refusedFor([](json &m) {
        m["definition"]["rules"].push_back({{"type", "CALENDAR"},
                                            {"comparison", ">="},
                                            {"target", 1},
                                            {"reference", "weekday=mon"}});
    }));
    CHECK(refusedFor([](json &m) { m["attempt"] = "../../etc/passwd"; }));
    CHECK(refusedFor([](json &m) { m["checksum"] = "CA5F"; }));
    CHECK(refusedFor([](json &m) { m["position"] = 7; }));
}

TEST_CASE("Device messages match the API's examples", "[competitions]")
{
    const auto identity = parsed().identity;
    CHECK(goalInstalledMessage(identity, STARTED_AT_MS - 2130) == json::parse(GOAL_INSTALLED));

    const AttemptStart start {SESSION, 1, STARTED_AT_MS, "b7e1c0de-5a1e-4c00-9d00-000000000001",
                              1041};
    CHECK(attemptStartedMessage(identity, start) == json::parse(ATTEMPT_STARTED));
}

TEST_CASE("Receipts are matched to the message and attempt they answer", "[competitions]")
{
    const auto identity = parsed().identity;
    CHECK(parseReceipt(json::parse(GOAL_INSTALLED_SAVED), goalInstalledType(), identity)
          == ReceiptStatus::Saved);
    CHECK(parseReceipt(json::parse(ATTEMPT_STARTED_SAVED), attemptStartedType(), identity)
          == ReceiptStatus::Saved);

    auto duplicate = json::parse(ATTEMPT_STARTED_SAVED);
    duplicate["status"] = "duplicate";
    CHECK(parseReceipt(duplicate, attemptStartedType(), identity) == ReceiptStatus::Duplicate);

    // Another message's receipt, another attempt's, or another revision's answers nothing
    CHECK_FALSE(parseReceipt(json::parse(GOAL_INSTALLED_SAVED), attemptStartedType(), identity));
    auto other = json::parse(ATTEMPT_STARTED_SAVED);
    other["attempt"] = "0193a5e0-0000-7000-8000-000000000009";
    CHECK_FALSE(parseReceipt(other, attemptStartedType(), identity));
    other = json::parse(ATTEMPT_STARTED_SAVED);
    other["checksum"] = std::string(64, '0');
    CHECK_FALSE(parseReceipt(other, attemptStartedType(), identity));
}

TEST_CASE("UTC times are written with milliseconds", "[competitions]")
{
    CHECK(formatUtc(STARTED_AT_MS) == "2026-11-06T20:00:00.250Z");
    CHECK(formatUtc(0) == "1970-01-01T00:00:00.000Z");
}

TEST_CASE("A release parses as the API sends it", "[competitions]")
{
    std::string error;
    const auto release = parseAttemptControl(json::parse(ATTEMPT_CONTROL), error);
    REQUIRE(release);
    CHECK(release->identity == parsed().identity);
    CHECK(release->control == CONTROL);
}

TEST_CASE("An attempt control other than a release with its id is refused", "[competitions]")
{
    auto refusedWith = [](const char *key, const json &value) {
        auto message = json::parse(ATTEMPT_CONTROL);
        message[key] = value;
        std::string error;
        const bool refused = !parseAttemptControl(message, error);
        CHECK_FALSE(error.empty());
        return refused;
    };
    CHECK(refusedWith("action", "cancel"));
    CHECK(refusedWith("control", nullptr));
    CHECK(refusedWith("control", "not-a-uuid"));
    CHECK(refusedWith("version", 2));
    CHECK(refusedWith("type", "competition.goal"));

    auto missing = json::parse(ATTEMPT_CONTROL);
    missing.erase("control");
    std::string error;
    CHECK_FALSE(parseAttemptControl(missing, error));
}

TEST_CASE("attempt_released matches the API's example", "[competitions]")
{
    std::string error;
    const auto release = parseAttemptControl(json::parse(ATTEMPT_CONTROL), error);
    REQUIRE(release);
    const ReleaseApplied applied {ReleaseState::Released, "b7e1c0de-5a1e-4c00-9d00-000000000001",
                                  1077};
    CHECK(attemptReleasedMessage(*release, applied) == json::parse(ATTEMPT_RELEASED));

    auto started = applied;
    started.state = ReleaseState::Started;
    CHECK(attemptReleasedMessage(*release, started)["state"] == "started");
    started.state = ReleaseState::Unknown;
    CHECK(attemptReleasedMessage(*release, started)["state"] == "unknown");
}

TEST_CASE("A release's receipt is matched to the release it answers", "[competitions]")
{
    const auto identity = parsed().identity;
    const auto saved = json::parse(ATTEMPT_RELEASED_SAVED);
    CHECK(parseReceipt(saved, attemptReleasedType(), identity, CONTROL) == ReceiptStatus::Saved);

    auto other = saved;
    other["control"] = "0193a5e0-0000-7000-8000-000000000010";
    CHECK_FALSE(parseReceipt(other, attemptReleasedType(), identity, CONTROL));
}

// ---- Control channel ---------------------------------------------------------------------------

namespace {

/// Destroyed after the Net below: the Net's event manager holds a strand on it.
struct IoHolder {
    boost::asio::io_context io;
};

/// A Net whose competition.goal handling is observed: the goals handed over, the events raised.
struct GoalNet : IoHolder, Net {
    GoalNet()
        : Net({}, {})
    {
        NetTestAccess::setEventManager(
                *this, std::make_shared<EventManager>(
                               boost::asio::make_strand(io), [this](const EventBase &event) {
                                   scorbit::CompetitionGoal goal;
                                   if (Event(&event).getCompetitionGoal(goal)) {
                                       events.push_back(goal);
                                   }
                               }));
        setCompetitionGoalCallback([this](competitions::CompetitionGoal goal) {
            handedOver.push_back(std::move(goal));
        });
        setAttemptReleaseCallback([this](competitions::AttemptRelease release) {
            releases.push_back(std::move(release));
        });
    }

    void handle(const json &payload)
    {
        NetTestAccess::handleCompetitionGoal(*this, payload);
        io.run_for(std::chrono::milliseconds {50});
        io.restart();
    }

    std::vector<competitions::CompetitionGoal> handedOver;
    std::vector<competitions::AttemptRelease> releases;
    std::vector<scorbit::CompetitionGoal> events;
};

} // namespace

TEST_CASE("Net hands a competition.goal over to be installed", "[competitions]")
{
    GoalNet net;
    net.handle(goal());
    REQUIRE(net.handedOver.size() == 1);
    CHECK(net.handedOver[0].identity.attempt == ATTEMPT);
    CHECK(net.events.empty()); // raised once Scorbit confirms the install
}

TEST_CASE("Net refuses a competition.goal it can't judge, and raises nothing", "[competitions]")
{
    GoalNet net;
    auto message = goal();
    message["definition"]["timer_policy"]["name"] = "whole_game";
    net.handle(message);

    CHECK(net.handedOver.empty());
    CHECK(net.events.empty());
}

TEST_CASE("Net hands a release over to be applied, and refuses any other control", "[competitions]")
{
    GoalNet net;
    NetTestAccess::handleAttemptControl(net, json::parse(ATTEMPT_CONTROL));
    REQUIRE(net.releases.size() == 1);
    CHECK(net.releases[0].control == CONTROL);
    CHECK(net.releases[0].identity.attempt == ATTEMPT);

    auto cancel = json::parse(ATTEMPT_CONTROL);
    cancel["action"] = "cancel";
    NetTestAccess::handleAttemptControl(net, cancel);
    CHECK(net.releases.size() == 1);
}

// ---- Outbox ------------------------------------------------------------------------------------

TEST_CASE("Outbox sends one message at a time, in order, resent unchanged", "[competitions]")
{
    MessageOutbox outbox;
    const OutgoingMessage installed {"a", MessageKind::GoalInstalled, "{1}"};
    const OutgoingMessage started {"a", MessageKind::AttemptStarted, "{2}"};
    outbox.add(installed);
    outbox.add(started);
    outbox.add(installed); // queued already

    CHECK(outbox.next() == installed);
    CHECK_FALSE(outbox.next()); // one in flight
    outbox.add(installed);      // in flight already

    CHECK(outbox.failed() == MessageOutbox::INITIAL_BACKOFF);
    CHECK_FALSE(outbox.next()); // backing off
    outbox.retryReady();
    CHECK(outbox.next() == installed); // first again, unchanged

    CHECK(outbox.failed() == 2 * MessageOutbox::INITIAL_BACKOFF);
    outbox.retryReady();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(outbox.next());
        CHECK(outbox.failed() <= MessageOutbox::MAX_BACKOFF);
        outbox.retryReady();
    }

    REQUIRE(outbox.next() == installed);
    outbox.answered();
    CHECK(outbox.next() == started);
    CHECK(outbox.failed() == MessageOutbox::INITIAL_BACKOFF); // reset by the answer
    outbox.retryReady();
    REQUIRE(outbox.next());
    outbox.answered();
    CHECK(outbox.empty());
}

TEST_CASE("Outbox lets other attempts past one that keeps failing", "[competitions]")
{
    MessageOutbox outbox;
    const OutgoingMessage aInstalled {"a", MessageKind::GoalInstalled, "{a1}"};
    const OutgoingMessage aStarted {"a", MessageKind::AttemptStarted, "{a2}"};
    const OutgoingMessage bInstalled {"b", MessageKind::GoalInstalled, "{b1}"};
    outbox.add(aInstalled);
    outbox.add(bInstalled);
    outbox.add(aStarted);

    REQUIRE(outbox.next() == aInstalled);
    outbox.failed();
    outbox.retryReady();

    // b goes first; a's install still goes before its start
    CHECK(outbox.next() == bInstalled);
    outbox.answered();
    CHECK(outbox.next() == aInstalled);
    outbox.answered();
    CHECK(outbox.next() == aStarted);
}

TEST_CASE("Outbox keeps a release's answer when its attempt is dropped", "[competitions]")
{
    MessageOutbox outbox;
    const OutgoingMessage started {"a", MessageKind::AttemptStarted, "{a2}"};
    const OutgoingMessage released {"a", MessageKind::AttemptReleased, "{a3}", "c"};
    outbox.add(started);
    outbox.add(released);

    outbox.drop("a");
    CHECK(outbox.next() == released);
    outbox.answered();
    CHECK(outbox.empty());
}

TEST_CASE("Outbox sends a release's answer ahead of the start it holds up", "[competitions]")
{
    MessageOutbox outbox;
    const OutgoingMessage bInstalled {"b", MessageKind::GoalInstalled, "{b1}"};
    const OutgoingMessage started {"a", MessageKind::AttemptStarted, "{a2}"};
    const OutgoingMessage released {"a", MessageKind::AttemptReleased, "{a3}", "c"};
    outbox.add(started);
    outbox.add(bInstalled);

    REQUIRE(outbox.next() == started);
    outbox.failed();
    outbox.addFirst(released);
    outbox.addFirst(released); // once
    outbox.retryReady();

    CHECK(outbox.next() == released);
    outbox.answered();
    CHECK(outbox.next() == bInstalled);
    outbox.answered();
    CHECK(outbox.next() == started);
    outbox.answered();
    CHECK(outbox.empty());
}

// ---- Service -----------------------------------------------------------------------------------

TEST_CASE("A goal is held, then goal_installed is sent", "[competitions]")
{
    Fixture f;
    f.deliver();

    // Held in memory, with nothing written, by the time it is acknowledged
    REQUIRE(f.game->competitions().attempts().contains(ATTEMPT));
    CHECK(f.record().goal.message == goal());
    CHECK_FALSE(boost::filesystem::exists(boost::filesystem::path(f.dir.str()) / "competitions"));
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].kind == MessageKind::GoalInstalled);
    CHECK(f.net->sent[0].body["type"] == "competition.goal_installed");
    CHECK(f.net->sent[0].body["attempt"] == ATTEMPT);
    CHECK(f.net->goals.empty()); // the host hears of it once Scorbit confirms it

    f.answer(receipt(GOAL_INSTALLED_SAVED));
    CHECK(f.record().installed == Delivery::Saved);
    REQUIRE(f.net->goals.size() == 1);
    CHECK(f.net->goals[0].attempt == ATTEMPT);
    CHECK(f.net->goals[0].player == PLAYER);
    CHECK(f.net->goals[0].position == 0);
    CHECK(f.net->goals[0].name == "Ali Baba Express");
}

TEST_CASE("A goal Scorbit had saved already is confirmed too", "[competitions]")
{
    // The first answer was lost: the resend is answered `duplicate`
    Fixture f;
    f.deliver();
    f.answer(noAnswer());
    CHECK(f.net->goals.empty());
    REQUIRE(f.net->retry);
    f.net->retry();
    f.run();

    auto duplicate = json::parse(GOAL_INSTALLED_SAVED);
    duplicate["status"] = "duplicate";
    f.answer(receipt(duplicate.dump().c_str()));
    CHECK(f.record().installed == Delivery::Duplicate);
    REQUIRE(f.net->goals.size() == 1);
    CHECK(f.net->goals[0].attempt == ATTEMPT);
}

TEST_CASE("A goal is confirmed once, after a failed send and a resend", "[competitions]")
{
    Fixture f;
    f.deliver();
    f.answer(ApiReply {Error::ApiError, 503, "busy", {}});
    CHECK(f.net->goals.empty());
    REQUIRE(f.net->retry);
    f.net->retry();
    f.run();

    f.answer(receipt(GOAL_INSTALLED_SAVED));
    CHECK(f.net->goals.size() == 1);

    // Its start, answered later, raises nothing more
    f.startGame();
    f.sessionCreated();
    f.answer(receipt(ATTEMPT_STARTED_SAVED));
    CHECK(f.net->goals.size() == 1);
}

TEST_CASE("Several competitions' goals are held at once", "[competitions]")
{
    Fixture f;
    f.install();
    auto other = goal();
    other["tournament"] = "0193a5e0-0000-7000-8000-000000000011";
    other["attempt"] = "0193a5e0-0000-7000-8000-000000000013";
    f.deliver(other);

    const auto &attempts = f.game->competitions().attempts();
    CHECK(attempts.size() == 2);
    CHECK(attempts.contains(ATTEMPT));
    CHECK(attempts.contains("0193a5e0-0000-7000-8000-000000000013"));
}

TEST_CASE("A repeated delivery of the same attempt changes nothing", "[competitions]")
{
    Fixture f;
    f.install();
    const auto before = f.record().installedAtMs;

    f.deliver();
    CHECK(f.net->sent.empty());
    CHECK(f.net->goals.size() == 1);
    CHECK(f.record().installedAtMs == before);
}

TEST_CASE("The same attempt with another player or revision is refused", "[competitions]")
{
    Fixture f;
    f.install();

    auto otherChecksum = goal();
    otherChecksum["checksum"] = std::string(64, 'b');
    f.deliver(otherChecksum);

    auto otherPlayer = goal();
    otherPlayer["player"] = "6f1c2d3e-4a5b-4c6d-8e7f-000000000099";
    f.deliver(otherPlayer);

    CHECK(f.net->goals.size() == 1); // only the install's confirmation; refusals raise nothing
    CHECK(f.net->sent.empty());
    CHECK(f.record().goal.message == goal()); // the installed one is kept as it was
}

TEST_CASE("The start is recorded once the session reply names the player", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();

    // The session uuid isn't known yet: nothing can be sent
    CHECK_FALSE(f.record().start);
    CHECK(f.net->sent.empty());

    f.sessionCreated(PLAYER, 2);
    REQUIRE(f.record().start);
    CHECK(f.record().start->session == SESSION);
    CHECK(f.record().start->position == 2);
    CHECK(f.record().start->bootId == f.net->boot);
    CHECK(f.record().start->sequence == 1);         // the boot's first row
    CHECK(f.record().started == Delivery::Pending); // kept until confirmed

    REQUIRE(f.net->sent.size() == 1);
    const auto &body = f.net->sent[0].body;
    CHECK(f.net->sent[0].kind == MessageKind::AttemptStarted);
    CHECK(body["type"] == "competition.attempt_started");
    CHECK(body["session"] == SESSION);
    CHECK(body["position"] == 2);
    CHECK(body["boot_id"] == f.net->boot);
    CHECK(body["sequence"] == 1);
    CHECK(body["started_at"] == formatUtc(f.record().start->startedAtMs));

    f.answer(receipt(ATTEMPT_STARTED_SAVED));
    CHECK(f.record().started == Delivery::Saved);
}

TEST_CASE("A player seated after the session uuid still starts the attempt", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    const auto session = f.sessionCreated("6f1c2d3e-4a5b-4c6d-8e7f-0000000000ff", 1); // another
    CHECK_FALSE(f.record().start);

    // A later update of the same session seats them
    f.net->seat(session, 3, PLAYER);
    f.run();
    REQUIRE(f.record().start);
    CHECK(f.record().start->position == 3);
    CHECK(f.net->sent.size() == 1);
}

TEST_CASE("Only the game's own session seats its players", "[competitions]")
{
    Fixture f;
    f.install();

    // The player was in the previous game, whose session is answered late
    f.startGame();
    const auto previous = f.sessionCreated("6f1c2d3e-4a5b-4c6d-8e7f-0000000000ff", 1);
    f.game->setGameFinished();
    f.startGame();
    f.sessionCreated("6f1c2d3e-4a5b-4c6d-8e7f-0000000000ee", 1);

    // A stale reply for an earlier, unknown session names the player: nothing starts
    f.net->seat("6f1c2d3e-4a5b-4c6d-8e7f-0000000000dd", 2, PLAYER);
    f.run();
    CHECK_FALSE(f.record().start);

    // A late update of the previous game's own session does: they played that game
    f.net->seat(previous, 2, PLAYER);
    f.run();
    REQUIRE(f.record().start);
    CHECK(f.record().start->session == previous);
    CHECK(f.record().start->position == 2);
}

TEST_CASE("An attempt admitted on another machine doesn't start here", "[competitions]")
{
    // Installed on the goal's machine, then the device switched to another
    Fixture f;
    f.install();
    f.net->venue = "6f1c2d3e-4a5b-4c6d-8e7f-0000000000aa";
    f.startGame();
    f.sessionCreated();
    CHECK_FALSE(f.record().start);
    CHECK(f.net->sent.empty());

    // Back on its machine, it starts
    f.game->setGameFinished();
    f.net->venue = "6f1c2d3e-4a5b-4c6d-8e7f-000000000005";
    f.startGame();
    f.sessionCreated();
    CHECK(f.record().start);
}

TEST_CASE("Only the competition the player's slot is for starts", "[competitions]")
{
    // The player chose competition A, then re-tapped for B before playing: A's attempt is still
    // open, and both goals are installed. A's token sorts first, as UUIDv7s issued earlier do.
    constexpr auto TOURNAMENT_B = "0193a5e0-0000-7000-8000-000000000011";
    constexpr auto ATTEMPT_B = "0193a5e0-0000-7000-8000-000000000013";
    Fixture f;
    f.install();
    auto b = goal();
    b["tournament"] = TOURNAMENT_B;
    b["attempt"] = ATTEMPT_B;
    f.deliver(b);
    f.answer(receipt(R"({"version": 1, "type": "competition.goal_installed_saved",
        "attempt": "0193a5e0-0000-7000-8000-000000000013",
        "checksum": "ca5f30ab82d2a8d3a69f1041795a15f3f5e78abea9239f272793fd1253ee4d53",
        "status": "saved"})"));
    REQUIRE(f.net->sent.empty());

    // The slot's Entry is for B: only B starts, so A can't take the slot from it
    f.startGame();
    f.sessionCreated(PLAYER, 1, TOURNAMENT_B);
    const auto &attempts = f.game->competitions().attempts();
    CHECK_FALSE(attempts.at(ATTEMPT).start);
    REQUIRE(attempts.at(ATTEMPT_B).start);
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body["attempt"] == ATTEMPT_B);
    CHECK(f.net->sent[0].body["tournament"] == TOURNAMENT_B);
}

TEST_CASE("A slot with no competition yet starts nothing until a reply names it",
          "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    const auto session = f.sessionCreated(PLAYER, 1, "");
    CHECK_FALSE(f.record().start);
    CHECK(f.net->sent.empty());

    // A later reply for the same session shows the slot's Entry: it starts then, with the
    // game's own start time and sequence
    const auto startedAt = f.game->competitions().attempts().at(ATTEMPT).installedAtMs;
    f.net->seat(session, 1, PLAYER, "0193A5E0-0000-7000-8000-000000000001");
    f.run();
    REQUIRE(f.record().start);
    CHECK(f.record().start->sequence == 1);
    CHECK(f.record().start->startedAtMs >= startedAt);
    CHECK(f.net->sent.size() == 1);
}

TEST_CASE("A player beyond position 6 doesn't start the attempt", "[competitions]")
{
    // The API accepts positions 1 to 6: a start at 7 would be refused, ending the attempt
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated(PLAYER, 7);
    CHECK_FALSE(f.record().start);
    CHECK(f.net->sent.empty());
}

TEST_CASE("No attempt starts in a game timed by an unsynchronised clock", "[competitions]")
{
    Fixture f;
    f.install();
    f.net->clockSynchronised = false;
    f.startGame();
    f.sessionCreated();
    CHECK_FALSE(f.record().start);

    // It waits for a game with a trusted start time
    f.game->setGameFinished();
    f.net->clockSynchronised = true;
    f.startGame();
    f.sessionCreated();
    CHECK(f.record().start);
}

TEST_CASE("An attempt installed after its game started doesn't start in it", "[competitions]")
{
    Fixture f;
    f.startGame();
    f.install();
    f.sessionCreated();
    CHECK_FALSE(f.record().start);
    CHECK(f.net->sent.empty());

    // It starts in the next game
    f.game->setGameFinished();
    f.startGame();
    f.sessionCreated();
    CHECK(f.record().start);
}

TEST_CASE("An attempt starts once, never again in a later game", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    const auto start = f.record().start;
    REQUIRE(start);
    f.answer(receipt(ATTEMPT_STARTED_SAVED));

    f.game->setGameFinished();
    f.startGame();
    f.sessionCreated();
    CHECK(f.record().start == start);
    CHECK(f.net->sent.empty());
}

TEST_CASE("A start is numbered by its game's start row, in the order of all rows", "[competitions]")
{
    // The rows run on across games, whether or not a game starts anything: the first game's
    // start and end rows are 1 and 2
    Fixture f;
    f.startGame();
    f.game->setGameFinished();
    f.install();
    f.startGame();
    f.sessionCreated();
    REQUIRE(f.record().start);
    CHECK(f.record().start->sequence == 3);
}

TEST_CASE("A message without an answer is resent unchanged, with backoff", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    REQUIRE(f.net->sent.size() == 1);
    const auto body = f.net->sent[0].body;

    f.answer(noAnswer());
    CHECK(f.net->sent.empty());
    CHECK(f.net->retryDelay == MessageOutbox::INITIAL_BACKOFF);
    REQUIRE(f.net->retry);
    f.net->retry();
    f.run();

    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body == body);

    f.answer(ApiReply {Error::ApiError, 503, "busy", {}});
    CHECK(f.net->retryDelay == 2 * MessageOutbox::INITIAL_BACKOFF);
    f.net->retry();
    f.run();
    CHECK(f.net->sent[0].body == body);

    f.answer(receipt(ATTEMPT_STARTED_SAVED));
    CHECK(f.record().started == Delivery::Saved);
    CHECK(f.net->sent.empty());
}

TEST_CASE("Only a 409 refuses: any other failure is resent", "[competitions]")
{
    // A failed re-authentication, a refused or malformed request, or an API not deployed yet
    for (const int status : {400, 401, 403, 404, 408, 429}) {
        Fixture f;
        f.deliver();
        f.answer(ApiReply {Error::ApiError, status, R"({"code": "x", "detail": "y"})", {}});
        CHECK(f.record().installed == Delivery::Pending);
        REQUIRE(f.net->retry);
        f.net->retry();
        f.run();
        REQUIRE(f.net->sent.size() == 1);
        CHECK(f.net->sent[0].kind == MessageKind::GoalInstalled);
    }

    // A 200 that isn't a receipt, as from a captive portal
    Fixture f;
    f.deliver();
    f.answer(receipt("<html>"));
    CHECK(f.record().installed == Delivery::Pending);
    CHECK(f.net->retry);
}

TEST_CASE("An attempt the server keeps failing doesn't hold up another", "[competitions]")
{
    // The API doesn't know the first attempt (as after a switch to another server): it answers
    // 400 every time, never a receipt or a 409
    Fixture f;
    f.deliver();
    auto other = goal();
    other["tournament"] = "0193a5e0-0000-7000-8000-000000000011";
    other["attempt"] = "0193a5e0-0000-7000-8000-000000000013";
    f.deliver(other);

    f.answer(ApiReply {Error::ApiError, 400, R"({"code": "invalid_request", "detail": "x"})", {}});
    REQUIRE(f.net->retry);
    f.net->retry();
    f.run();

    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body["attempt"] == "0193a5e0-0000-7000-8000-000000000013");
    auto saved = json::parse(GOAL_INSTALLED_SAVED);
    saved["tournament"] = other["tournament"];
    saved["attempt"] = other["attempt"];
    f.answer(receipt(saved.dump().c_str()));
    CHECK(f.game->competitions().attempts().at(other["attempt"].get<std::string>()).installed
          == Delivery::Saved);

    // The first is still resent
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body["attempt"] == ATTEMPT);
    CHECK(f.record().installed == Delivery::Pending);
}

TEST_CASE("A named refusal stops the resending and forgets the attempt", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();

    f.answer(refused(ATTEMPT_REFUSED));
    CHECK_FALSE(f.game->competitions().attempts().contains(ATTEMPT));
    CHECK(f.net->sent.empty());
    CHECK_FALSE(f.net->retry);
}

TEST_CASE("A refused install never starts, nor sends a start queued behind it",
          "[competitions]")
{
    Fixture f;
    f.deliver();
    // The start is queued while the install is still unanswered
    f.startGame();
    f.sessionCreated();
    REQUIRE(f.net->sent.size() == 1);

    f.answer(refused(R"({"code": "attempt_ended", "detail": "Ended."})"));
    CHECK_FALSE(f.game->competitions().attempts().contains(ATTEMPT));
    CHECK(f.net->sent.empty());
    CHECK(f.net->goals.empty()); // never confirmed, so the host never hears of it

    f.game->setGameFinished();
    f.startGame();
    f.sessionCreated();
    CHECK(f.net->sent.empty());
}

TEST_CASE("A goal for another venue machine is refused", "[competitions]")
{
    Fixture f;
    f.net->venue = "6f1c2d3e-4a5b-4c6d-8e7f-0000000000aa";
    f.deliver();
    CHECK(f.net->sent.empty());
    CHECK(f.net->goals.empty());

    // Compared without regard to case
    Fixture g;
    g.net->venue = "6F1C2D3E-4A5B-4C6D-8E7F-000000000005";
    g.deliver();
    REQUIRE(g.net->sent.size() == 1);
    CHECK(g.net->sent[0].kind == MessageKind::GoalInstalled);
}

TEST_CASE("A restart loses the attempt", "[competitions]")
{
    // Installed and started, its start still unanswered
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    REQUIRE(f.net->sent.size() == 1);

    // Nothing comes back: the attempt is over, and nothing of it is sent again
    f.boot();
    CHECK(f.game->competitions().attempts().empty());
    CHECK(f.net->sent.empty());

    // Its player playing again starts nothing, until the API sends a new goal
    f.startGame();
    f.sessionCreated();
    CHECK(f.game->competitions().attempts().empty());
    CHECK(f.net->sent.empty());
}

TEST_CASE("The start's row order runs on across games, and from 1 again each boot",
          "[competitions]")
{
    constexpr auto SECOND_TOURNAMENT = "0193a5e0-0000-7000-8000-000000000011";
    auto second = goal();
    second["tournament"] = SECOND_TOURNAMENT;
    second["attempt"] = "0193a5e0-0000-7000-8000-000000000013";
    const auto secondAttempt = [](const Fixture &f) -> const AttemptRecord & {
        return f.game->competitions().attempts().at("0193a5e0-0000-7000-8000-000000000013");
    };

    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    REQUIRE(f.record().start);
    CHECK(f.record().start->sequence == 1);

    // A later game's start row: after game 1's start, session reply and end rows
    f.game->setGameFinished();
    f.deliver(second);
    f.startGame();
    f.sessionCreated(PLAYER, 1, SECOND_TOURNAMENT);
    REQUIRE(secondAttempt(f).start);
    CHECK(secondAttempt(f).start->sequence == 4);
    const auto firstBoot = f.net->boot;

    // A new boot counts from 1 again; its boot id tells it apart
    f.boot();
    f.deliver(second);
    f.startGame();
    f.sessionCreated(PLAYER, 1, SECOND_TOURNAMENT);
    REQUIRE(secondAttempt(f).start);
    CHECK(secondAttempt(f).start->sequence == 1);
    CHECK(secondAttempt(f).start->bootId == f.net->boot);
    CHECK(f.net->boot != firstBoot);
}

// ---- Goals judged afresh in each designated game (SB-6366) -------------------------------------

namespace {

constexpr auto ALI_BABA = "NA:Tale of Ali Baba";
constexpr auto OTHER_PLAYER = "6f1c2d3e-4a5b-4c6d-8e7f-0000000000ff";
constexpr auto SECOND = "0193a5e0-0000-7000-8000-000000000023";

json rule(const char *type, const char *comparison, int64_t target, json reference = nullptr)
{
    return {{"type", type},
            {"comparison", comparison},
            {"target", target},
            {"reference", std::move(reference)}};
}

/** The example goal, for @p attempt, with @p rules instead of goal 115's when given. */
json goalFor(const std::string &attempt, std::optional<json> rules = std::nullopt)
{
    auto message = goal();
    message["attempt"] = attempt;
    if (rules) {
        message["definition"]["rules"] = std::move(*rules);
    }
    return message;
}

const AttemptRecord &attemptOf(const Fixture &f, const std::string &attempt)
{
    return f.game->competitions().attempts().at(attempt);
}

void activate(Fixture &f, const std::string &mode = ALI_BABA)
{
    f.game->addMode(mode);
    f.game->commit();
    f.run();
}

void deactivate(Fixture &f, const std::string &mode = ALI_BABA)
{
    f.game->removeMode(mode);
    f.game->commit();
    f.run();
}

void score(Fixture &f, sb_score_t value, sb_player_t player = 1)
{
    f.game->setScore(player, value);
    f.game->commit();
    f.run();
}

void addSpins(Fixture &f, int64_t value)
{
    f.game->addEvent("spins", value);
    f.game->commit();
    f.run();
}

void activePlayer(Fixture &f, sb_player_t player)
{
    f.game->setActivePlayer(player);
    f.game->commit();
    f.run();
}

int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

/** Goal 115's original, as the profile's definitions feed delivers it. */
std::string ordinaryFeed(int64_t modeTarget = 1)
{
    const json modeRule = {{"type", "MODE"},
                           {"comparison", ">="},
                           {"target", modeTarget},
                           {"reference", ALI_BABA},
                           {"subachievement", nullptr}};
    const json scoreRule = {{"type", "SCORE"},
                            {"comparison", ">="},
                            {"target", 1},
                            {"reference", nullptr},
                            {"subachievement", nullptr}};
    const json original = {{"key", "game-totan-achievement-tournament"},
                           {"name", "Ali Baba Express"},
                           {"scope", "game"},
                           {"is_single_session", true},
                           {"frame", nullptr},
                           {"frame_version", 0},
                           {"rules", json::array({modeRule, scoreRule})}};
    return json {{"game", "totan"},
                 {"timezone", nullptr},
                 {"utc_offsets", nullptr},
                 {"calendar", json::object()},
                 {"results", json::array({original})}}
            .dump();
}

/** The progress reply: the player holds goal 115's original. */
std::string originalHeld()
{
    return json::array({{{"id", "6d1f2a90"},
                         {"achievement",
                          {{"id", "0b7c4f0e"},
                           {"key", "game-totan-achievement-tournament"},
                           {"scope", "game"}}},
                         {"current_value", 1},
                         {"achieved", true},
                         {"achieved_time", nullptr},
                         {"progress", 0}}})
            .dump();
}

} // namespace

TEST_CASE("Ali Baba then the score: the goal is met at the score", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    REQUIRE(f.record().start);

    activate(f);
    CHECK_FALSE(f.record().qualification); // the score is still 0

    const auto before = nowMs();
    score(f, 10);
    REQUIRE(f.record().qualification);
    const auto &met = *f.record().qualification;
    CHECK(met.completionUtcMs >= before);
    CHECK(met.completionUtcMs <= nowMs());
    // Rows: the start, the session reply's, Ali Baba, then the score
    CHECK(met.startSequence == 1);
    CHECK(met.completionSequence == 4);
    CHECK(met.utcSynchronised);
    CHECK(met.evidence.bootId == f.net->boot);
}

TEST_CASE("The score then Ali Baba: the goal is met at Ali Baba", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();

    score(f, 10);
    CHECK_FALSE(f.record().qualification);

    activate(f);
    REQUIRE(f.record().qualification);
    CHECK(f.record().qualification->completionSequence == 4);
}

TEST_CASE("Ali Baba needn't still be running when the score is reached", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();

    activate(f);
    deactivate(f);
    f.game->setCurrentBall(2);
    f.game->commit();
    f.run();
    CHECK_FALSE(f.record().qualification);

    score(f, 1);
    CHECK(f.record().qualification);
}

TEST_CASE("Owning the original badge gives no credit and never suppresses the goal",
          "[competitions]")
{
    Fixture f;
    REQUIRE(f.net->definitionsReply);
    f.net->definitionsReply(ApiReply {Error::Success, 200, ordinaryFeed(), {}});
    f.run();
    f.install();
    f.startGame();
    f.net->claim(1, PLAYER);
    f.sessionCreated();
    REQUIRE(f.record().start);
    REQUIRE(f.net->progressUserId == PLAYER);
    f.net->progressReply(ApiReply {Error::Success, 200, originalHeld(), {}});
    f.run();
    const auto view = f.game->achievements().view();
    REQUIRE(view->players.count(1));
    CHECK(view->players.at(1).userId == PLAYER);

    // Nothing fresh: no qualification
    f.game->setCurrentBall(2);
    f.game->commit();
    f.run();
    CHECK_FALSE(f.record().qualification);

    // Fresh play still meets it, though the original is held
    activate(f);
    score(f, 10);
    CHECK(f.record().qualification);

    // The qualification is no ordinary award: nothing is reported or unlocked for the original
    f.game->setGameFinished();
    f.run();
    CHECK(f.net->reports.empty());
    CHECK(f.net->unlocks.empty());
}

TEST_CASE("Owning the original and playing nothing never qualifies", "[competitions]")
{
    Fixture f;
    REQUIRE(f.net->definitionsReply);
    f.net->definitionsReply(ApiReply {Error::Success, 200, ordinaryFeed(), {}});
    f.run();
    f.install();
    f.startGame();
    f.net->claim(1, PLAYER);
    f.sessionCreated();
    f.net->progressReply(ApiReply {Error::Success, 200, originalHeld(), {}});
    f.run();

    f.game->setGameFinished();
    f.run();
    REQUIRE(f.record().start);
    CHECK_FALSE(f.record().qualification);
}

TEST_CASE("Progress split across games never qualifies", "[competitions]")
{
    Fixture f;
    f.install();

    // Game 1 is the first attempt's: only the score
    f.startGame();
    f.sessionCreated();
    score(f, 10);
    f.game->setGameFinished();
    f.run();

    // Game 2 is the second attempt's: only Ali Baba. It starts afresh at zero.
    f.deliver(goalFor(SECOND));
    f.startGame();
    f.sessionCreated();
    activate(f);
    f.game->setGameFinished();
    f.run();

    REQUIRE(f.record().start);
    CHECK_FALSE(f.record().qualification);
    REQUIRE(attemptOf(f, SECOND).start);
    CHECK(attemptOf(f, SECOND).start->session != f.record().start->session);
    CHECK_FALSE(attemptOf(f, SECOND).qualification);
}

TEST_CASE("A completed attempt doesn't spare the next one its fresh game", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    activate(f);
    score(f, 10);
    REQUIRE(f.record().qualification);
    const auto first = *f.record().qualification;
    f.game->setGameFinished();
    f.run();

    // Attempt 2 of the same competition, in the next game: it starts from zero
    f.deliver(goalFor(SECOND));
    f.startGame();
    f.sessionCreated();
    REQUIRE(attemptOf(f, SECOND).start);

    score(f, 10);
    CHECK_FALSE(attemptOf(f, SECOND).qualification);
    activate(f);
    REQUIRE(attemptOf(f, SECOND).qualification);
    CHECK(attemptOf(f, SECOND).qualification->completionSequence > first.completionSequence);
    // The first keeps its own
    CHECK(f.record().qualification->completionSequence == first.completionSequence);
    CHECK(f.record().qualification->durationMs == first.durationMs);
}

TEST_CASE("A goal met before the reply seats its player qualifies at the original row",
          "[competitions]")
{
    // Ali Baba exactly once: it holds, then stops holding at the second activation
    Fixture f;
    f.deliver(goalFor(ATTEMPT,
                      json::array({rule("MODE", "=", 1, ALI_BABA), rule("SCORE", ">=", 1)})));
    f.startGame();

    activate(f);
    score(f, 10);
    const auto metBy = nowMs();
    deactivate(f);
    activate(f);
    CHECK_FALSE(f.record().qualification); // not seated yet

    // The reply arrives only after the game ended: no row follows it
    f.game->setGameFinished();
    f.run();
    f.sessionCreated();
    REQUIRE(f.record().start);
    REQUIRE(f.record().qualification);
    CHECK(f.record().qualification->completionUtcMs <= metBy);
    // Rows: the start, Ali Baba, then the score
    CHECK(f.record().qualification->completionSequence == 3);
}

TEST_CASE("Only the attempt's own position meets its goal", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated(PLAYER, 2);
    REQUIRE(f.record().start);

    // Player 1 does it all: not the attempt's player
    activate(f);
    score(f, 10, 1);
    deactivate(f);
    CHECK_FALSE(f.record().qualification);

    // Player 2's turn
    activePlayer(f, 2);
    score(f, 10, 2);
    CHECK_FALSE(f.record().qualification);
    activate(f);
    CHECK(f.record().qualification);
}

TEST_CASE("A rule that becomes false again is met only while every rule holds together",
          "[competitions]")
{
    const auto onceAndScore =
            json::array({rule("MODE", "=", 1, ALI_BABA), rule("SCORE", ">=", 1000)});

    SECTION("MODE = 1: a second activation before the score ends the chance")
    {
        Fixture f;
        f.deliver(goalFor(ATTEMPT, onceAndScore));
        f.startGame();
        f.sessionCreated();

        activate(f); // MODE = 1 holds, SCORE doesn't yet
        deactivate(f);
        activate(f); // MODE is 2 now
        score(f, 1000);
        f.game->setGameFinished();
        f.run();
        // Each rule held at some time, never both at once
        CHECK_FALSE(f.record().qualification);
    }

    SECTION("MODE = 1: the score during the single activation meets it, for good")
    {
        Fixture f;
        f.deliver(goalFor(ATTEMPT, onceAndScore));
        f.startGame();
        f.sessionCreated();

        activate(f);
        score(f, 1000);
        REQUIRE(f.record().qualification);
        const auto met = *f.record().qualification;

        deactivate(f);
        activate(f); // no longer holds, but the goal was met
        CHECK(f.record().qualification->completionSequence == met.completionSequence);
        CHECK(f.record().qualification->durationMs == met.durationMs);
    }

    SECTION("EVENT: a register that falls back must reach the target again")
    {
        Fixture f;
        f.deliver(goalFor(ATTEMPT, json::array({rule("EVENT", ">=", 3, "spins"),
                                                rule("MODE", ">=", 1, ALI_BABA)})));
        f.startGame();
        f.sessionCreated();

        addSpins(f, 3);
        addSpins(f, -2);
        activate(f);
        CHECK_FALSE(f.record().qualification); // spins is 1 when Ali Baba starts

        addSpins(f, 2);
        CHECK(f.record().qualification);
    }

    SECTION("TIMER_SESSION <=: met in time")
    {
        Fixture f;
        f.deliver(goalFor(ATTEMPT, json::array({rule("TIMER_SESSION", "<=", 60'000),
                                                rule("MODE", ">=", 1, ALI_BABA)})));
        f.startGame();
        f.sessionCreated();
        activate(f);
        CHECK(f.record().qualification);
    }
}

TEST_CASE("A goal an earlier moment would meet is refused, not approximated", "[competitions]")
{
    auto refusal = [](json rules) {
        std::string error;
        const bool accepted = parseGoal(goalFor(ATTEMPT, std::move(rules)), error).has_value();
        return accepted ? std::string {} : error;
    };

    // The start's 0 would meet it, whatever the score is later
    CHECK(refusal(json::array({rule("SCORE", "<=", 1000), rule("MODE", ">=", 1, ALI_BABA)}))
                  .find("one instant")
          != std::string::npos);
    CHECK_FALSE(refusal(json::array({rule("SCORE", "=", 0)})).empty());
    CHECK_FALSE(refusal(json::array({rule("SCORE", "!=", 0)})).empty());
    // A ball or activation still running is measured so far
    CHECK_FALSE(refusal(json::array({rule("TIMER_BALL", "<=", 10'000)})).empty());
    CHECK_FALSE(refusal(json::array({rule("TIMER_MODE", "=", 5'000, ALI_BABA)})).empty());
    // Judged on what is true at the row: accepted
    CHECK(refusal(json::array({rule("SCORE", ">=", 1)})).empty());
    CHECK(refusal(json::array({rule("TIMER_SESSION", "<=", 60'000)})).empty());
    CHECK(refusal(json::array({rule("TIMER_BALL", ">=", 10'000)})).empty());
    CHECK(refusal(json::array({rule("MODE", "<=", 1, ALI_BABA)})).empty());
    CHECK(refusal(json::array({rule("MODE_SCORE", "<=", 1000, ALI_BABA)})).empty());

    // A lifetime goal would carry progress from other games
    auto lifetime = goal();
    lifetime["definition"]["achievement"]["is_single_session"] = false;
    std::string error;
    CHECK_FALSE(parseGoal(lifetime, error));
    CHECK(error.find("single session") != std::string::npos);
}

TEST_CASE("The goal is the saved copy, in its saved order, whatever the profile holds",
          "[competitions]")
{
    Fixture f;
    auto reordered = goal();
    auto &rules = reordered["definition"]["rules"];
    std::swap(rules[0], rules[1]);
    f.deliver(reordered);
    REQUIRE(f.record().goal.goal.rules.size() == 2);
    CHECK(f.record().goal.goal.rules[0].type == achievements::RuleType::Score);
    CHECK(f.record().goal.goal.rules[1].type == achievements::RuleType::Mode);

    // The profile refreshes with an edited original, asking for Ali Baba twice
    REQUIRE(f.net->definitionsReply);
    f.net->definitionsReply(ApiReply {Error::Success, 200, ordinaryFeed(2), {}});
    f.run();
    REQUIRE(f.game->achievements().view()->definitions->size() == 1);

    f.startGame();
    f.sessionCreated();
    activate(f);
    score(f, 10);
    CHECK(f.record().qualification); // the copy asks for once
}

TEST_CASE("Several competitions' goals are each judged by their own copy", "[competitions]")
{
    constexpr auto SECOND_TOURNAMENT = "0193a5e0-0000-7000-8000-000000000031";
    constexpr auto THIEVES = "NA:Forty Thieves";
    auto second = goalFor(SECOND, json::array({rule("MODE", ">=", 1, THIEVES)}));
    second["tournament"] = SECOND_TOURNAMENT;
    second["rules"] = "0193a5e0-0000-7000-8000-000000000032";
    second["checksum"] = std::string(64, 'b');
    second["player"] = OTHER_PLAYER;

    Fixture f;
    f.install();
    f.deliver(second);
    f.startGame();
    REQUIRE(f.net->sessionCreated);
    f.net->sessionPlayers(SESSION, {{1, SessionSeat {PLAYER, TOURNAMENT}},
                                    {2, SessionSeat {OTHER_PLAYER, SECOND_TOURNAMENT}}});
    f.net->sessionCreated(SESSION);
    f.run();
    REQUIRE(f.record().start);
    REQUIRE(attemptOf(f, SECOND).start);

    // Player 1 meets goal 115; their Forty Thieves isn't player 2's
    activate(f);
    activate(f, THIEVES);
    score(f, 10);
    CHECK(f.record().qualification);
    CHECK_FALSE(attemptOf(f, SECOND).qualification);

    // Player 2 meets the second competition's goal
    activePlayer(f, 2);
    deactivate(f, THIEVES);
    activate(f, THIEVES);
    CHECK(attemptOf(f, SECOND).qualification);
}

TEST_CASE("A goal met when the game ends qualifies at the end", "[competitions]")
{
    // A MODE_SCORE <= rule has no verdict while its mode's activation runs: it has one once the
    // activation closes, here at the end. (With no activation at all it holds, as 0: hence MODE.)
    Fixture f;
    f.deliver(goalFor(ATTEMPT, json::array({rule("MODE_SCORE", "<=", 1000, ALI_BABA),
                                            rule("MODE", ">=", 1, ALI_BABA)})));
    f.startGame();
    f.sessionCreated();
    activate(f);
    score(f, 500);
    CHECK_FALSE(f.record().qualification); // still running

    f.game->setGameFinished();
    f.run();
    CHECK(f.record().qualification);
}

TEST_CASE("A goal met when the game ends can't qualify if the end row was lost", "[competitions]")
{
    Fixture f;
    f.deliver(goalFor(ATTEMPT, json::array({rule("MODE_SCORE", "<=", 1000, ALI_BABA),
                                            rule("MODE", ">=", 1, ALI_BABA)})));
    f.startGame();
    f.sessionCreated();
    activate(f);
    score(f, 500);

    // The end row is dropped, though counted: what happened up to it is unknown
    f.net->auth = AuthStatus::AuthenticationFailed;
    f.game->setGameFinished();
    f.run();
    CHECK_FALSE(f.record().qualification);
}

TEST_CASE("A lost row stops the timing: no result shorter than was played", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();

    // A commit dropped while auth has failed keeps its place in the row order
    f.net->auth = AuthStatus::AuthenticationFailed;
    activePlayer(f, 2);
    f.net->auth = AuthStatus::AuthenticatedPaired;

    activePlayer(f, 1);
    activate(f);
    score(f, 10);
    CHECK_FALSE(f.record().qualification);
}

TEST_CASE("A goal met on a row whose clock wasn't vouched for qualifies, flagged", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();

    // Only the completion's UTC is in doubt: the duration is on the elapsed clock
    f.net->clockSynchronised = false;
    activate(f);
    score(f, 10);
    REQUIRE(f.record().qualification);
    CHECK_FALSE(f.record().qualification->utcSynchronised);
}

TEST_CASE("An attempt seated late is timed by its own position's turns only", "[competitions]")
{
    using achievements::TimelineRow;
    FakeNet net {""};
    CompetitionService service {net, [](std::function<void()> fn) { fn(); },
                                [] { return STARTED_AT_MS; }, net.boot};
    service.onGoal(parsed(goalFor(ATTEMPT)));

    // P1 0–30 s, then P2, who meets goal 115 at 60 s: 30 s of P2's own turn. No one is seated yet.
    // Rows: UTC, player, ball, scores, modes, completed, events, synchronised, elapsed, sequence
    const int64_t t = STARTED_AT_MS;
    const std::map<achievements::PlayerNumber, int64_t> zero {{1, 0}, {2, 0}};
    service.onGameStarted(1, TimelineRow {t, 1, 1, zero, {}, {}, {}, true, 0, 1});
    service.onRow(TimelineRow {t + 30'000, 2, 1, zero, {}, {}, {}, true, 30'000, 2});
    service.onRow(TimelineRow {t + 50'000, 2, 1, zero, {ALI_BABA}, {}, {}, true, 50'000, 3});
    service.onRow(
            TimelineRow {t + 60'000, 2, 1, {{1, 0}, {2, 10}}, {ALI_BABA}, {}, {}, true, 60'000, 4});
    service.onRow(
            TimelineRow {t + 90'000, 1, 1, {{1, 0}, {2, 10}}, {ALI_BABA}, {}, {}, true, 90'000, 5});
    const auto &record = service.attempts().at(ATTEMPT);
    CHECK_FALSE(record.qualification);

    // The reply seats the attempt's player at position 2
    service.onSessionPlayers(SESSION, {{2, SessionSeat {PLAYER, TOURNAMENT}}});
    service.onSessionCreated(1, SESSION);
    REQUIRE(record.start);
    CHECK(record.start->sequence == 1);
    REQUIRE(record.qualification);
    CHECK(record.qualification->durationMs == 30'000);
    CHECK(record.qualification->startSequence == 1);
    CHECK(record.qualification->completionSequence == 4);
    CHECK(record.qualification->completionUtcMs == t + 60'000);
    CHECK(record.qualification->evidence.bootId == net.boot);
}

TEST_CASE("A late reply for an earlier game never lets a later game qualify its attempt",
          "[competitions]")
{
    Fixture f;
    f.install();

    // Game A: its session reply is slow
    f.startGame();
    auto replyA = f.net->sessionCreated;
    f.game->setGameFinished();
    f.run();

    // Game B: the goal is met at position 1, before any reply seats anyone
    f.startGame();
    activate(f);
    score(f, 10);

    // A's reply arrives: the attempt started in A, where it met nothing
    f.net->seat(SESSION, 1, PLAYER);
    replyA(SESSION);
    f.run();
    REQUIRE(f.record().start);
    CHECK(f.record().start->session == SESSION);

    // More of game B, then B's own reply: neither qualifies A's attempt
    score(f, 20);
    f.sessionCreated(PLAYER, 1);
    score(f, 30);
    CHECK_FALSE(f.record().qualification);
}

TEST_CASE("A game replaced without an end is judged once more on its last row", "[competitions]")
{
    using achievements::TimelineRow;
    FakeNet net {""};
    CompetitionService service {net, [](std::function<void()> fn) { fn(); },
                                [] { return STARTED_AT_MS; }, net.boot};
    service.onGoal(parsed(goalFor(ATTEMPT, json::array({rule("MODE_SCORE", "<=", 1000, ALI_BABA),
                                                        rule("MODE", ">=", 1, ALI_BABA)}))));

    // Rows: UTC, player, ball, scores, modes, completed, events, synchronised, elapsed, sequence
    const int64_t t = STARTED_AT_MS;
    service.onGameStarted(1, TimelineRow {t, 1, 1, {{1, 0}}, {}, {}, {}, true, 0, 1});
    service.onRow(TimelineRow {t + 1000, 1, 1, {{1, 0}}, {ALI_BABA}, {}, {}, true, 1000, 2});
    service.onRow(TimelineRow {t + 2000, 1, 1, {{1, 500}}, {ALI_BABA}, {}, {}, true, 2000, 3});
    service.onSessionPlayers(SESSION, {{1, SessionSeat {PLAYER, TOURNAMENT}}});
    service.onSessionCreated(1, SESSION);
    const auto &record = service.attempts().at(ATTEMPT);
    REQUIRE(record.start);
    CHECK_FALSE(record.qualification); // Ali Baba still running

    SECTION("The next game's start row follows: game 1 ended on its last row")
    {
        service.onGameStarted(2, TimelineRow {t + 5000, 1, 1, {{1, 0}}, {}, {}, {}, true, 5000, 4});
        REQUIRE(record.qualification);
        CHECK(record.qualification->completionSequence == 3);
        CHECK(record.qualification->completionUtcMs == t + 2000);
        CHECK(record.qualification->durationMs == 2000);
    }

    SECTION("Rows went missing before the next game's start: nothing is timed to them")
    {
        service.onGameStarted(2, TimelineRow {t + 5000, 1, 1, {{1, 0}}, {}, {}, {}, true, 5000, 6});
        CHECK_FALSE(record.qualification);
    }
}

TEST_CASE("A goal's timer rules are judged on the elapsed clock, whatever UTC does",
          "[competitions]")
{
    using achievements::TimelineRow;
    FakeNet net {""};
    CompetitionService service {net, [](std::function<void()> fn) { fn(); },
                                [] { return STARTED_AT_MS; }, net.boot};
    service.onGoal(parsed(goalFor(ATTEMPT, json::array({rule("TIMER_SESSION", ">=", 60'000)}))));

    // Rows: UTC, player, ball, scores, modes, completed, events, synchronised, elapsed, sequence
    const int64_t t = STARTED_AT_MS;
    const std::map<achievements::PlayerNumber, int64_t> zero {{1, 0}};
    service.onGameStarted(1, TimelineRow {t, 1, 1, zero, {}, {}, {}, true, 0, 1});
    service.onSessionPlayers(SESSION, {{1, SessionSeat {PLAYER, TOURNAMENT}}});
    service.onSessionCreated(1, SESSION);
    const auto &record = service.attempts().at(ATTEMPT);
    REQUIRE(record.start);

    // A second in, UTC is stepped a minute forward: one second played, not sixty-one
    service.onRow(TimelineRow {t + 61'000, 1, 1, zero, {}, {}, {}, true, 1000, 2});
    CHECK_FALSE(record.qualification);
    // Then two minutes back: nothing played is taken away
    service.onRow(TimelineRow {t - 59'000, 1, 1, zero, {}, {}, {}, true, 59'000, 3});
    CHECK_FALSE(record.qualification);
    service.onRow(TimelineRow {t - 58'000, 1, 1, zero, {}, {}, {}, true, 60'000, 4});
    REQUIRE(record.qualification);
    CHECK(record.qualification->durationMs == 60'000);
    CHECK(record.qualification->completionSequence == 4);
    CHECK(record.qualification->completionUtcMs == t - 58'000); // still the row's own UTC
}

TEST_CASE("TIMER_BETWEEN is refused where a later occurrence would shorten its span",
          "[competitions]")
{
    auto accepted = [](json rules) {
        std::string error;
        return parseGoal(goalFor(ATTEMPT, std::move(rules)), error).has_value();
    };
    const auto a = rule("MODE", ">=", 1, ALI_BABA);
    const auto b = rule("MODE", ">=", 1, "NA:Forty Thieves");

    // Two siblings: the shortest covering span only shrinks as the ball goes on
    CHECK_FALSE(accepted(json::array({rule("TIMER_BETWEEN", ">=", 8000), a, b})));
    CHECK_FALSE(accepted(json::array({rule("TIMER_BETWEEN", "=", 8000), a, b})));
    CHECK(accepted(json::array({rule("TIMER_BETWEEN", "<=", 8000), a, b})));
    // One sibling: gaps between consecutive occurrences never change once there
    CHECK(accepted(json::array({rule("TIMER_BETWEEN", ">=", 8000), a})));
}

// ---- Releases (SB-6609) ------------------------------------------------------------------------

namespace {

/** The release's answer, as the API saves it, with @p status. */
ApiReply releaseSaved(const char *status = "saved")
{
    auto saved = json::parse(ATTEMPT_RELEASED_SAVED);
    saved["status"] = status;
    return receipt(saved.dump().c_str());
}

/** Installs the example goal, starts its game and has the API save the start. */
void startAttempt(Fixture &f)
{
    f.install();
    f.startGame();
    f.sessionCreated();
    f.answer(receipt(ATTEMPT_STARTED_SAVED));
    REQUIRE(f.record().started == Delivery::Saved);
}

const ReleaseRecord &onlyRelease(const Fixture &f)
{
    REQUIRE(f.game->competitions().releases().size() == 1);
    return f.game->competitions().releases().front();
}

} // namespace

TEST_CASE("A release of a running attempt drops it and says where", "[competitions]")
{
    Fixture f;
    startAttempt(f);
    activate(f);

    f.release();
    CHECK_FALSE(f.game->competitions().attempts().contains(ATTEMPT));
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].kind == MessageKind::AttemptReleased);
    const auto &body = f.net->sent[0].body;
    CHECK(body["type"] == "competition.attempt_released");
    CHECK(body["attempt"] == ATTEMPT);
    CHECK(body["control"] == CONTROL);
    CHECK(body["state"] == "released");
    CHECK(body["boot_id"] == f.net->boot);
    // Rows: the start, the session reply's, then Ali Baba: applied after the last
    CHECK(body["sequence"] == 3);
    CHECK(body["qualification"].is_null());

    // It stops timing: the score that would have met its goal counts for nothing
    score(f, 10);
    CHECK_FALSE(f.game->competitions().attempts().contains(ATTEMPT));

    f.answer(releaseSaved());
    CHECK(onlyRelease(f).delivery == Delivery::Saved);
    CHECK(f.net->sent.empty());
}

TEST_CASE("A release before the start drops the attempt, which never starts", "[competitions]")
{
    // Released while its game is under way, before a reply seats its player
    Fixture f;
    f.install();
    f.startGame();
    f.release();
    CHECK_FALSE(f.game->competitions().attempts().contains(ATTEMPT));
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body["state"] == "released");
    CHECK(f.net->sent[0].body["sequence"] == 1); // after the game's start row
    f.answer(releaseSaved());

    // The reply seating its player starts nothing, and a late resend of the goal is refused
    f.sessionCreated();
    f.deliver();
    CHECK(f.net->sent.empty());
    CHECK(f.game->competitions().attempts().empty());
}

TEST_CASE("A release whose install is unanswered drops the install too", "[competitions]")
{
    Fixture f;
    f.deliver();
    REQUIRE(f.net->sent.size() == 1); // the install, in flight
    f.release();
    CHECK_FALSE(f.game->competitions().attempts().contains(ATTEMPT));

    // The install's failure is let go, not resent: only the release's answer follows
    f.answer(noAnswer());
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].kind == MessageKind::AttemptReleased);
    CHECK(f.net->sent[0].body["state"] == "released");
    CHECK(f.net->goals.empty());
}

TEST_CASE("A release of an attempt not held here is answered unknown", "[competitions]")
{
    // As after a restart: nothing of the attempt is left
    Fixture f;
    f.release();
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body["state"] == "unknown");
    CHECK(f.net->sent[0].body["sequence"] == 0); // no row yet this boot
    CHECK(f.net->sent[0].body["boot_id"] == f.net->boot);
}

TEST_CASE("A repeated release changes nothing and gets the same answer", "[competitions]")
{
    Fixture f;
    startAttempt(f);
    f.release();
    REQUIRE(f.net->sent.size() == 1);
    const auto answer = f.net->sent[0].body;

    // While its answer is on its way: nothing more is sent
    f.release();
    CHECK(f.net->sent.size() == 1);
    f.answer(releaseSaved());

    // Rows later: the same answer, the same place in the row order
    activate(f);
    f.release();
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body == answer);
    f.answer(releaseSaved("duplicate"));
    CHECK(onlyRelease(f).delivery == Delivery::Duplicate);
    CHECK(f.net->sent.empty());
}

TEST_CASE("A qualification before the release comes first in the row order", "[competitions]")
{
    Fixture f;
    startAttempt(f);
    activate(f);
    score(f, 10);
    REQUIRE(f.record().qualification);
    const auto completion = f.record().qualification->completionSequence;

    f.release();
    REQUIRE(f.net->sent.size() == 1);
    const auto &body = f.net->sent[0].body;
    CHECK(body["state"] == "released");
    // Not after the release: the API keeps the result
    CHECK(body["sequence"].get<uint64_t>() >= completion);
    // Its report id is B02F's (SB-6370), which reports the qualification
    CHECK(body["qualification"].is_null());
}

TEST_CASE("A release before the server has the start keeps the attempt: started", "[competitions]")
{
    // The start is on its way when the release arrives: the server refuses it, the release
    // pending, so the game began before the release reached the machine
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    REQUIRE(f.net->sent.size() == 1);
    const auto start = f.net->sent[0].body;

    f.release();
    CHECK(f.net->sent.size() == 1); // decided by the start's answer
    CHECK_FALSE(onlyRelease(f).decided);
    CHECK(f.game->competitions().attempts().contains(ATTEMPT));

    f.answer(refused(RELEASE_PENDING));
    CHECK(onlyRelease(f).decided);
    CHECK(f.game->competitions().attempts().contains(ATTEMPT));

    // The release's answer goes first, then the start again, unchanged
    f.retry();
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].kind == MessageKind::AttemptReleased);
    CHECK(f.net->sent[0].body["state"] == "started");
    CHECK(f.net->sent[0].body["sequence"] == 2); // the start, then the session reply's
    f.answer(releaseSaved());

    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body == start);
    f.answer(receipt(ATTEMPT_STARTED_SAVED));
    CHECK(f.record().started == Delivery::Saved);
    REQUIRE(f.record().start);
    CHECK(f.record().start->session == SESSION);

    // It plays on, and still qualifies
    activate(f);
    score(f, 10);
    CHECK(f.record().qualification);
}

TEST_CASE("A release while the start is unanswered: a receipt means an unclaim", "[competitions]")
{
    // The server had the start already (its first answer was lost): the release is an unclaim
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    f.release();
    CHECK(f.net->sent.size() == 1); // the start, in flight

    f.answer(receipt(ATTEMPT_STARTED_SAVED));
    CHECK_FALSE(f.game->competitions().attempts().contains(ATTEMPT));
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body["state"] == "released");
    CHECK(f.net->sent[0].body["sequence"] == 2); // where it arrived, not where it was decided
}

TEST_CASE("A start refused release_pending is resent until the server saves it", "[competitions]")
{
    // The release hasn't reached the machine yet: the API resends it at the player's next tap
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    const auto start = f.net->sent[0].body;

    for (int i = 0; i < 3; ++i) {
        f.answer(refused(RELEASE_PENDING));
        CHECK(f.game->competitions().attempts().contains(ATTEMPT));
        f.retry();
        REQUIRE(f.net->sent.size() == 1);
        CHECK(f.net->sent[0].body == start);
    }
    f.answer(receipt(ATTEMPT_STARTED_SAVED));
    CHECK(f.record().started == Delivery::Saved);
}

TEST_CASE("A release's answer is resent until answered, and a refusal is final", "[competitions]")
{
    Fixture f;
    f.release();
    const auto answer = f.net->sent[0].body;

    f.answer(ApiReply {Error::ApiError, 503, "busy", {}});
    f.retry();
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body == answer);

    f.answer(refused(R"({"code": "control_mismatch", "detail": "Acknowledged otherwise."})"));
    CHECK(f.game->competitions().releases().empty());
    CHECK(f.net->sent.empty());
}

TEST_CASE("A release arriving between a start's resends is answered first: started",
          "[competitions]")
{
    // The start is refused release_pending before the release reaches the machine
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    const auto start = f.net->sent[0].body;
    f.answer(refused(RELEASE_PENDING));
    f.retry();
    REQUIRE(f.net->sent.size() == 1); // the start again, in flight

    // Then it arrives, twice: one release, still waiting for the start's answer
    f.release();
    f.release();
    CHECK(f.net->sent.size() == 1);
    CHECK_FALSE(onlyRelease(f).decided);

    f.answer(refused(RELEASE_PENDING));
    f.retry();
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body["state"] == "started");

    // Its answer gets no reply: it still goes before the start, which waits for it
    f.answer(noAnswer());
    f.retry();
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].kind == MessageKind::AttemptReleased);
    f.answer(releaseSaved());
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body == start);
}

TEST_CASE("A refused started answer leaves the attempt released", "[competitions]")
{
    // The server had the start after all: `started` is refused wrong_state
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    f.release();
    f.answer(refused(RELEASE_PENDING));
    f.retry();
    REQUIRE(f.net->sent[0].body["state"] == "started");

    f.answer(refused(R"({"code": "wrong_state", "detail": "Already started."})"));
    CHECK_FALSE(f.game->competitions().attempts().contains(ATTEMPT));
    CHECK(f.net->sent.empty()); // nor is its start resent
    activate(f);
    score(f, 10);
    CHECK(f.game->competitions().attempts().empty());
}

TEST_CASE("A refused start answers the release waiting on it: released", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    f.release();

    f.answer(refused(ATTEMPT_REFUSED));
    CHECK_FALSE(f.game->competitions().attempts().contains(ATTEMPT));
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body["state"] == "released");
}
