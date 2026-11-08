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
#include "competitions/attempt_store.h"
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
    explicit FakeNet(std::string dataDir) { m_info.dataDir = std::move(dataDir); }

    /** As the SDK runs with no data dir configured. */
    void forgetDataDir() { m_info.dataDir.clear(); }

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
    bool isClockSynchronised() const override { return clockSynchronised; }
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
    void publishEvent(EventPtr event) override
    {
        if (auto *e = dynamic_cast<CompetitionGoalReceivedEvent *>(event.get())) {
            goals.push_back({e->installed(), e->attempt(), e->player(), e->position(),
                             e->reason()});
        }
    }

    /** A reply for @p session seats @p userId at @p position. */
    void seat(const std::string &session, sb_player_t position, const std::string &userId)
    {
        sessionPlayers(session, {{position, userId}});
    }

    struct Sent {
        MessageKind kind;
        json body;
        ApiReplyCallback reply;
    };

    SessionCreatedCallback sessionCreated;
    SessionPlayersCallback sessionPlayers;
    bool clockSynchronised {true};
    std::string venue {"6f1c2d3e-4a5b-4c6d-8e7f-000000000005"};
    CompetitionGoalCallback goalReceived;
    std::deque<Sent> sent;
    std::function<void()> retry;
    std::chrono::steady_clock::duration retryDelay {};
    std::vector<scorbit::CompetitionGoal> goals;

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
    void boot(bool withDataDir = true)
    {
        game.reset();
        auto fake = std::make_unique<FakeNet>(dir.str());
        if (!withDataDir) {
            fake->forgetDataDir();
        }
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
    std::string sessionCreated(const std::string &userId = PLAYER, sb_player_t position = 1)
    {
        REQUIRE(net->sessionCreated);
        const auto session = sessions++ == 0
                                     ? std::string {SESSION}
                                     : fmt::format("6f1c2d3e-4a5b-4c6d-8e7f-0000000001{:02}",
                                                   sessions);
        net->seat(session, position, userId);
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
    CHECK(parsedGoal.definition == goal()["definition"]);

    auto withPosition = goal();
    withPosition["position"] = 2;
    CHECK(parsed(withPosition).position == 2);
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
    }

    void handle(const json &payload)
    {
        NetTestAccess::handleCompetitionGoal(*this, payload);
        io.run_for(std::chrono::milliseconds {50});
        io.restart();
    }

    std::vector<competitions::CompetitionGoal> handedOver;
    std::vector<scorbit::CompetitionGoal> events;
};

} // namespace

TEST_CASE("Net hands a competition.goal over to be installed", "[competitions]")
{
    GoalNet net;
    net.handle(goal());
    REQUIRE(net.handedOver.size() == 1);
    CHECK(net.handedOver[0].identity.attempt == ATTEMPT);
    CHECK(net.events.empty()); // raised once it is installed or refused
}

TEST_CASE("Net refuses a competition.goal it can't judge, and says so", "[competitions]")
{
    GoalNet net;
    auto message = goal();
    message["definition"]["timer_policy"]["name"] = "whole_game";
    message["position"] = 2;
    net.handle(message);

    CHECK(net.handedOver.empty());
    REQUIRE(net.events.size() == 1);
    CHECK_FALSE(net.events[0].installed);
    CHECK(net.events[0].attempt == ATTEMPT);
    CHECK(net.events[0].player == PLAYER);
    CHECK(net.events[0].position == 2);
    CHECK(net.events[0].reason.find("timer policy") != std::string::npos);
}

// ---- Store -------------------------------------------------------------------------------------

TEST_CASE("Attempts are kept on disk, several at once", "[competitions]")
{
    TempDir dir;
    AttemptStore store {dir.str()};

    AttemptRecord first;
    first.goal = parsed();
    first.installedAtMs = STARTED_AT_MS - 2130;
    first.installed = Delivery::Saved;
    first.start = AttemptStart {SESSION, 1, STARTED_AT_MS, "boot", 7};
    REQUIRE(store.save(first));

    auto message = goal();
    message["attempt"] = "0193a5e0-0000-7000-8000-000000000013";
    AttemptRecord second;
    second.goal = parsed(message);
    REQUIRE(store.save(second));

    // Kept apart from the ordinary definitions cache
    CHECK(boost::filesystem::exists(boost::filesystem::path(dir.str()) / "competitions"
                                    / "attempts" / (std::string(ATTEMPT) + ".json")));

    auto loaded = AttemptStore {dir.str()}.load();
    REQUIRE(loaded.size() == 2);
    std::ranges::sort(loaded, {}, [](const auto &r) { return r.goal.identity.attempt; });
    CHECK(loaded[0].goal.message == first.goal.message);
    CHECK(loaded[0].installedAtMs == first.installedAtMs);
    CHECK(loaded[0].installed == Delivery::Saved);
    CHECK(loaded[0].start == first.start);
    CHECK(loaded[0].started == Delivery::Pending);
    CHECK_FALSE(loaded[1].start);
}

TEST_CASE("A file that doesn't parse is skipped, not fatal", "[competitions]")
{
    TempDir dir;
    AttemptStore store {dir.str()};
    AttemptRecord record;
    record.goal = parsed();
    REQUIRE(store.save(record));
    std::ofstream {(store.root() / "attempts" / "broken.json").string()} << "{";

    CHECK(AttemptStore {dir.str()}.load().size() == 1);
}

TEST_CASE("The device event sequence is monotonic and survives a restart", "[competitions]")
{
    TempDir dir;
    {
        AttemptStore store {dir.str()};
        CHECK(store.nextSequence() == 1);
        CHECK(store.nextSequence() == 2);
    }
    AttemptStore store {dir.str()};
    CHECK(store.nextSequence() == 3);
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

// ---- Service -----------------------------------------------------------------------------------

TEST_CASE("A goal is saved before goal_installed is sent", "[competitions]")
{
    Fixture f;
    f.deliver();

    // On disk by the time it is acknowledged
    REQUIRE(AttemptStore {f.dir.str()}.load().size() == 1);
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].kind == MessageKind::GoalInstalled);
    CHECK(f.net->sent[0].body["type"] == "competition.goal_installed");
    CHECK(f.net->sent[0].body["attempt"] == ATTEMPT);

    REQUIRE(f.net->goals.size() == 1);
    CHECK(f.net->goals[0].installed);
    CHECK(f.net->goals[0].attempt == ATTEMPT);
    CHECK(f.net->goals[0].player == PLAYER);
    CHECK(f.net->goals[0].position == 0);

    f.answer(receipt(GOAL_INSTALLED_SAVED));
    CHECK(f.record().installed == Delivery::Saved);
    CHECK(AttemptStore {f.dir.str()}.load()[0].installed == Delivery::Saved);
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

    REQUIRE(f.net->goals.size() == 3);
    CHECK_FALSE(f.net->goals[1].installed);
    CHECK_FALSE(f.net->goals[1].reason.empty());
    CHECK_FALSE(f.net->goals[2].installed);
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
    CHECK(f.record().start->bootId == bootId());
    CHECK(f.record().start->sequence == 1);
    // Saved before it was sent
    CHECK(AttemptStore {f.dir.str()}.load()[0].start == f.record().start);

    REQUIRE(f.net->sent.size() == 1);
    const auto &body = f.net->sent[0].body;
    CHECK(f.net->sent[0].kind == MessageKind::AttemptStarted);
    CHECK(body["type"] == "competition.attempt_started");
    CHECK(body["session"] == SESSION);
    CHECK(body["position"] == 2);
    CHECK(body["boot_id"] == bootId());
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

TEST_CASE("A game with nothing to start takes no sequence number", "[competitions]")
{
    Fixture f;
    f.startGame();
    f.game->setGameFinished();
    f.install();
    f.startGame();
    f.sessionCreated();
    REQUIRE(f.record().start);
    CHECK(f.record().start->sequence == 1);
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

TEST_CASE("A named refusal stops the resending and forgets the attempt", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();

    f.answer(refused(ATTEMPT_REFUSED));
    CHECK_FALSE(f.game->competitions().attempts().contains(ATTEMPT));
    CHECK(AttemptStore {f.dir.str()}.load().empty());
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

    f.game->setGameFinished();
    f.startGame();
    f.sessionCreated();
    CHECK(f.net->sent.empty());
}

TEST_CASE("Without a data dir a goal is refused, not acknowledged", "[competitions]")
{
    Fixture f;
    f.boot(false);
    f.deliver();
    CHECK(f.net->sent.empty());
    REQUIRE(f.net->goals.size() == 1);
    CHECK_FALSE(f.net->goals[0].installed);
    CHECK(f.net->goals[0].reason.find("data dir") != std::string::npos);
}

TEST_CASE("A goal for another venue machine is refused", "[competitions]")
{
    Fixture f;
    f.net->venue = "6f1c2d3e-4a5b-4c6d-8e7f-0000000000aa";
    f.deliver();
    CHECK(f.net->sent.empty());
    REQUIRE(f.net->goals.size() == 1);
    CHECK_FALSE(f.net->goals[0].installed);

    // Compared without regard to case
    Fixture g;
    g.net->venue = "6F1C2D3E-4A5B-4C6D-8E7F-000000000005";
    g.deliver();
    CHECK(g.net->goals.at(0).installed);
}

TEST_CASE("Unanswered messages are sent again after a restart, as saved", "[competitions]")
{
    Fixture f;
    f.install();
    f.startGame();
    f.sessionCreated();
    REQUIRE(f.net->sent.size() == 1);
    const auto body = f.net->sent[0].body;

    f.boot();
    REQUIRE(f.net->sent.size() == 1);
    CHECK(f.net->sent[0].body == body);
    f.answer(receipt(ATTEMPT_STARTED_SAVED));

    // The sequence carries on from where it was
    f.boot();
    CHECK(f.net->sent.empty());
    auto message = goal();
    message["attempt"] = "0193a5e0-0000-7000-8000-000000000013";
    f.deliver(message);
    f.answer(receipt(R"({"version": 1, "type": "competition.goal_installed_saved",
        "attempt": "0193a5e0-0000-7000-8000-000000000013",
        "checksum": "ca5f30ab82d2a8d3a69f1041795a15f3f5e78abea9239f272793fd1253ee4d53",
        "status": "saved"})"));
    f.startGame();
    f.sessionCreated();
    CHECK(f.game->competitions()
                  .attempts()
                  .at("0193a5e0-0000-7000-8000-000000000013")
                  .start->sequence
          == 2);
}
