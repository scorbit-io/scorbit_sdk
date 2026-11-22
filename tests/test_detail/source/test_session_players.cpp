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

// Attract mode stage 1 on the device side (SPEC-0034): attract: and lobby: publications reach the
// app unparsed, and the running session's players reach it as a baseline and then as changes.

#include "session_players.h"
#include "net.h"
#include <scorbit_sdk/scorbit_sdk.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

using namespace scorbit;
using namespace scorbit::detail;
using json = nlohmann::json;

namespace {

constexpr auto VM = "7a1c9e52-0d3b-4f6a-9c11-5e2b8d4f0a77";
constexpr auto SESSION_A = "11111111-2222-3333-4444-555555555555";
constexpr auto SESSION_B = "66666666-7777-8888-9999-000000000000";

json score(unsigned position, json player = nullptr)
{
    return {{"id", 1000 + position}, {"position", position}, {"player", std::move(player)}, {"score", 0}};
}

json player(const std::string &id, const std::string &displayName)
{
    return {{"id", id},         {"username", id + "_user"}, {"display_name", displayName},
            {"initials", "XYZ"}, {"url", "https://example/u"}, {"avatar", nullptr}};
}

/// A Net whose events run on a local io_context, recording the new events; an unprovisioned Net
/// never starts its worker. io is declared first so it outlives the net's event manager.
struct AttractNet {
    boost::asio::io_context io;
    std::vector<std::pair<std::string, std::string>> attract;
    std::vector<std::pair<std::string, std::string>> lobby;
    std::vector<SessionPlayers> sessions;
    int other = 0;
    Net net {DeviceInfo {}, {}};

    AttractNet()
    {
        auto onEvent = [this](const EventBase &base) {
            const Event event(&base);
            std::string vm;
            std::string doc;
            SessionPlayers players;
            if (event.getAttractDocument(vm, doc)) {
                attract.emplace_back(vm, doc);
            } else if (event.getLobby(vm, doc)) {
                lobby.emplace_back(vm, doc);
            } else if (event.getSessionPlayers(players)) {
                sessions.push_back(std::move(players));
            } else {
                ++other;
            }
        };
        NetTestAccess::setEventManager(
                net, std::make_shared<EventManager>(boost::asio::make_strand(io), onEvent));
    }

    void publish(const std::string &channel, const json &data)
    {
        NetTestAccess::handlePublication(net, channel, data);
    }
    void drain() { io.run_for(std::chrono::milliseconds(50)); }
};

json attractDocument()
{
    return {{"type", "attract_document"},
            {"payload",
             {{"version", 1},
              {"enabled", true},
              {"items", json::array({{{"kind", "lobby"}}, {{"kind", "future_kind"}, {"x", 1}}})}}},
            {"metadata", {{"machine", 42}, {"venue", 7}}}};
}

} // namespace

// ---------------- sessionPlayersFromScores ----------------

TEST_CASE("sessionPlayersFromScores reads claim, id and display name per position")
{
    const auto players = sessionPlayersFromScores(
            json::array({score(2, player("u2", "Bob")), score(1), score(4, player("u4", ""))}));

    REQUIRE(players.size() == 4);
    CHECK_FALSE(players[0].claimed);
    CHECK(players[0].playerId.empty());
    CHECK(players[1] == SessionPlayerState {true, "u2", "Bob"});
    CHECK_FALSE(players[2].claimed); // skipped by the reply: unclaimed
    CHECK(players[3] == SessionPlayerState {true, "u4", ""});
}

TEST_CASE("sessionPlayersFromScores ignores entries without a usable position")
{
    const auto players = sessionPlayersFromScores(json::array(
            {json {{"position", 0}}, json {{"position", -1}}, json {{"position", "2"}},
             json {{"position", 99}, {"player", player("u", "x")}}, json("junk"), score(1)}));
    REQUIRE(players.size() == 1);
    CHECK_FALSE(players[0].claimed);
    CHECK(sessionPlayersFromScores(json::object()).empty());
}

// ---------------- SessionPlayersTracker ----------------

TEST_CASE("A session's first reply is its baseline; later ones only when something changed")
{
    SessionPlayersTracker tracker;
    const std::vector<SessionPlayerState> open {{}, {}};
    const std::vector<SessionPlayerState> claimed {{true, "u1", "Ann"}, {}};
    const std::vector<SessionPlayerState> renamed {{true, "u1", "Annie"}, {}};

    auto first = tracker.update(SESSION_A, open);
    REQUIRE(first);
    CHECK(first->isFirst);
    CHECK(first->players == open);

    CHECK_FALSE(tracker.update(SESSION_A, open)); // an unchanged refetch says nothing

    auto claim = tracker.update(SESSION_A, claimed);
    REQUIRE(claim);
    CHECK_FALSE(claim->isFirst);
    CHECK(claim->players == claimed);

    auto rename = tracker.update(SESSION_A, renamed);
    REQUIRE(rename);
    CHECK_FALSE(rename->isFirst);

    CHECK_FALSE(tracker.update("", open)); // no session, nothing to report
}

TEST_CASE("A new session gets its own baseline, and a late reply for the old one is no baseline")
{
    SessionPlayersTracker tracker;
    REQUIRE(tracker.update(SESSION_A, {{}})->isFirst);
    REQUIRE(tracker.update(SESSION_B, {{}})->isFirst);
    // The previous game's reply arrives after the next game started
    CHECK_FALSE(tracker.update(SESSION_A, {{}}));
}

TEST_CASE("The tracker keeps a few sessions, and clear forgets them all")
{
    SessionPlayersTracker tracker;
    for (const auto *uuid : {"s1", "s2", "s3", "s4", "s5"}) {
        REQUIRE(tracker.update(uuid, {{}})->isFirst);
    }
    CHECK(tracker.update("s1", {{}})->isFirst); // the oldest was dropped
    CHECK_FALSE(tracker.update("s5", {{}}));

    tracker.clear();
    CHECK(tracker.update("s5", {{}})->isFirst);
}

TEST_CASE("The refresh gate lets one fetch through until its token is gone")
{
    RefreshGate gate;
    auto first = gate.tryAcquire();
    REQUIRE(first);
    CHECK_FALSE(gate.tryAcquire()); // still pending: the tick is skipped

    auto copy = first; // a task's copies of its callback hold it too
    first.reset();
    CHECK_FALSE(gate.tryAcquire());
    copy.reset();

    auto next = gate.tryAcquire();
    CHECK(next);

    // A token outliving its gate releases harmlessly
    auto gone = std::make_unique<RefreshGate>();
    auto orphan = gone->tryAcquire();
    gone.reset();
    orphan.reset();
}

// ---------------- attract: and lobby: publications ----------------

TEST_CASE("Each attract: and lobby: publication raises one event, with its venue machine and JSON")
{
    AttractNet n;
    const auto doc = attractDocument();
    const json lobby {{"type", "setup_game"},
                      {"payload", {{"players", json::array({{{"player", player("u1", "Ann")}}})}}},
                      {"metadata", {{"created_at", "x"}}}};
    const json clearedLobby {{"type", "setup_game"}, {"payload", json::object()}};

    n.publish(std::string("attract:") + VM, doc);
    n.publish(std::string("lobby:") + VM, lobby);
    n.publish(std::string("lobby:") + VM, clearedLobby);
    n.drain();

    REQUIRE(n.attract.size() == 1);
    CHECK(n.attract[0].first == VM);
    CHECK(json::parse(n.attract[0].second) == doc); // unparsed by the SDK: everything kept
    REQUIRE(n.lobby.size() == 2);
    CHECK(n.lobby[0].first == VM);
    CHECK(json::parse(n.lobby[0].second) == lobby);
    CHECK(json::parse(n.lobby[1].second) == clearedLobby);
    CHECK(n.other == 0);
}

TEST_CASE("Look-alike and malformed attract and lobby channels raise nothing")
{
    AttractNet n;
    n.publish("attract:", attractDocument());           // no venue machine
    n.publish("attractions:x", attractDocument());      // not the attract: channel
    n.publish(std::string("lobbyist:") + VM, json {}); // not the lobby: channel
    n.publish(std::string("machine:") + VM, json {{"type", "attract_document"}});
    n.drain();
    CHECK(n.attract.empty());
    CHECK(n.lobby.empty());
    CHECK(n.other == 0);
}

TEST_CASE("sb_event_attract_document and sb_event_lobby read only their own event")
{
    const VenueMachineDocumentEvent attract(EventType::AttractDocumentReceived, VM, "{}");
    const VenueMachineDocumentEvent lobby(EventType::LobbyReceived, VM, "{\"a\":1}");
    const char *vm = nullptr;
    const char *doc = nullptr;

    CHECK(sb_event_attract_document(&attract, &vm, &doc));
    CHECK(std::string(vm) == VM);
    CHECK(std::string(doc) == "{}");
    CHECK_FALSE(sb_event_lobby(&attract, &vm, &doc));
    CHECK(sb_event_lobby(&lobby, &vm, &doc));
    CHECK(std::string(doc) == "{\"a\":1}");
    CHECK_FALSE(sb_event_attract_document(&lobby, &vm, &doc));
    CHECK_FALSE(sb_event_attract_document(nullptr, &vm, &doc));
    CHECK_FALSE(sb_event_attract_document(&attract, nullptr, &doc));
}

// ---------------- SessionPlayersChanged through Net ----------------

TEST_CASE("Session replies raise a baseline, then SessionPlayersChanged only on a change")
{
    AttractNet n;
    NetTestAccess::seedGameSession(n.net, 1, SESSION_A, true);

    const auto open = json::array({score(1), score(2)});
    const auto claimed = json::array({score(1), score(2, player("u2", "Bob"))});

    NetTestAccess::processScores(n.net, 1, open);    // the create reply at game start
    NetTestAccess::processScores(n.net, 1, open);    // a refetch that changes nothing
    NetTestAccess::processScores(n.net, 1, claimed); // a mid-game claim
    n.drain();

    REQUIRE(n.sessions.size() == 2);
    const auto &baseline = n.sessions[0];
    CHECK(baseline.sessionUuid == SESSION_A);
    CHECK(baseline.isFirst);
    CHECK(baseline.gameActive);
    REQUIRE(baseline.players.size() == 2);
    CHECK_FALSE(baseline.players[1].claimed);

    const auto &claim = n.sessions[1];
    CHECK_FALSE(claim.isFirst);
    REQUIRE(claim.players.size() == 2);
    CHECK(claim.players[1].position == 2);
    CHECK(claim.players[1].claimed);
    CHECK(claim.players[1].playerId == "u2");
    CHECK(claim.players[1].displayName == "Bob");
    CHECK_FALSE(claim.players[0].claimed);
}

TEST_CASE("A session without a uuid yet raises no SessionPlayersChanged")
{
    AttractNet n;
    NetTestAccess::seedGameSession(n.net, 1, "", true);
    NetTestAccess::processScores(n.net, 1, json::array({score(1)}));
    n.drain();
    CHECK(n.sessions.empty());
}

TEST_CASE("sb_event_session_player rejects a position out of range and a wrong event")
{
    const SessionPlayersChangedEvent event(SESSION_A, true, true,
                                           {SessionPlayerState {true, "u1", "Ann"}});
    const char *uuid = nullptr;
    bool isFirst = false;
    bool active = false;
    int count = 0;
    REQUIRE(sb_event_session_players(&event, &uuid, &isFirst, &active, &count));
    CHECK(std::string(uuid) == SESSION_A);
    CHECK(isFirst);
    CHECK(active);
    CHECK(count == 1);

    bool claimed = false;
    const char *id = nullptr;
    const char *name = nullptr;
    CHECK(sb_event_session_player(&event, 1, &claimed, &id, &name));
    CHECK(claimed);
    CHECK(std::string(name) == "Ann");
    CHECK_FALSE(sb_event_session_player(&event, 0, &claimed, &id, &name));
    CHECK_FALSE(sb_event_session_player(&event, 2, &claimed, &id, &name));

    const VenueMachineDocumentEvent lobby(EventType::LobbyReceived, VM, "{}");
    CHECK_FALSE(sb_event_session_players(&lobby, &uuid, &isFirst, &active, &count));
}

// ---------------- entry_activated and the running session ----------------

TEST_CASE("The running session is the newest active game with a uuid")
{
    AttractNet n;
    CHECK(NetTestAccess::activeSessionUuid(n.net).empty());
    NetTestAccess::seedGameSession(n.net, 1, SESSION_A, false); // finished
    NetTestAccess::seedGameSession(n.net, 2, "", true);         // no uuid yet
    CHECK(NetTestAccess::activeSessionUuid(n.net).empty());
    NetTestAccess::seedGameSession(n.net, 3, SESSION_B, true);
    CHECK(NetTestAccess::activeSessionUuid(n.net) == SESSION_B);
}

TEST_CASE("entry_activated with no running game is ignored without raising anything")
{
    AttractNet n;
    n.publish(std::string("control_machine:") + VM,
              json {{"type", "entry_activated"},
                    {"payload", {{"score_id", 1}, {"position", 2}, {"tournament_id", "t"}}}});
    n.drain();
    CHECK(n.sessions.empty());
    CHECK(n.other == 0);
}
