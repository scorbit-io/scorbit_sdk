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

#include "table_mappings_internal.h"
#include "event_classes.h"
#include "event_manager.h"
#include "net.h"
#include <scorbit_sdk/event.h>
#include <scorbit_sdk/event_helpers_c.h>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace scorbit;
using namespace scorbit::detail;

namespace {

constexpr auto TARGET = "6a0d1f7e-2b9c-4e8a-9d3f-1c2b3a4d5e6f";
constexpr auto CURRENT = "0f1e2d3c-4b5a-4968-8776-655443322110";

std::string mappingsBody(const std::string &venueMachine = TARGET)
{
    return nlohmann::json {{"server_time", "2026-11-02T10:00:00Z"},
                           {"mappings",
                            {{{"key_type", "script_id"},
                              {"table_key", "abc123"},
                              {"venuemachine", venueMachine}}}}}
            .dump();
}

std::string errorBody(const std::string &code)
{
    return nlohmann::json {{"type", "client_error"},
                           {"errors", {{{"code", code}, {"detail", "x"}, {"attr", nullptr}}}}}
            .dump();
}

cpr::Response response(int status, std::string body = {}, cpr::Header header = {})
{
    cpr::Response r;
    r.status_code = status;
    r.text = std::move(body);
    r.header = std::move(header);
    return r;
}

} // namespace

// ------------------------------------- parsing -------------------------------------------

TEST_CASE("apiErrorCode reads errors[0].code, then a top-level code")
{
    CHECK(apiErrorCode(errorBody("no_owner")) == "no_owner");
    CHECK(apiErrorCode(R"({"code":"not_vpin_device"})") == "not_vpin_device");
    CHECK(apiErrorCode(R"({"detail":"Not found."})").empty());
    CHECK(apiErrorCode("<html>").empty());
    CHECK(apiErrorCode("").empty());
}

TEST_CASE("parseTableMappings keeps the server's rows in order")
{
    const auto parsed = parseTableMappings(nlohmann::json {
            {"server_time", "t"},
            {"mappings",
             {{{"key_type", "a"}, {"table_key", "1"}, {"venuemachine", "u1"}},
              {{"key_type", "b"}, {"table_key", "2"}, {"venuemachine", "u2"}}}}}
                                                   .dump());
    REQUIRE(parsed);
    CHECK(parsed->serverTime == "t");
    REQUIRE(parsed->rows.size() == 2);
    CHECK(parsed->rows[0].keyType == "a");
    CHECK(parsed->rows[1].venueMachine == "u2");
}

TEST_CASE("parseTableMappings refuses a malformed list rather than a partial one")
{
    CHECK_FALSE(parseTableMappings(R"({"server_time":"t"})"));
    CHECK_FALSE(parseTableMappings(R"({"mappings":[{"key_type":"a","table_key":"1"}]})"));
    CHECK_FALSE(parseTableMappings(R"({"mappings":[1]})"));
    CHECK_FALSE(parseTableMappings("nope"));
    const auto empty = parseTableMappings(R"({"mappings":[]})");
    REQUIRE(empty);
    CHECK(empty->rows.empty());
}

TEST_CASE("A refused table-mappings read is told apart only by its code")
{
    CHECK(tableMappingsFailure(403, errorBody("not_vpin_device"))
          == SB_TABLE_MAPPINGS_NOT_VPIN_DEVICE);
    CHECK(tableMappingsFailure(403, errorBody("no_owner")) == SB_TABLE_MAPPINGS_NO_OWNER);
    CHECK(tableMappingsFailure(403, "") == SB_TABLE_MAPPINGS_UNAVAILABLE);
    CHECK(tableMappingsFailure(0, "") == SB_TABLE_MAPPINGS_UNAVAILABLE);
    CHECK(tableMappingsFailure(503, errorBody("no_owner")) == SB_TABLE_MAPPINGS_UNAVAILABLE);
}

TEST_CASE("switch-machine errors map by code, then by status")
{
    CHECK(switchMachineFailure(403, errorBody("no_owner")) == SB_SWITCH_MACHINE_NO_OWNER);
    CHECK(switchMachineFailure(403, errorBody("not_vpin_device"))
          == SB_SWITCH_MACHINE_NOT_VPIN_DEVICE);
    CHECK(switchMachineFailure(404, errorBody("venuemachine_not_found"))
          == SB_SWITCH_MACHINE_TARGET_NOT_FOUND);
    CHECK(switchMachineFailure(403, errorBody("venuemachine_not_owned"))
          == SB_SWITCH_MACHINE_TARGET_NOT_OWNED);
    CHECK(switchMachineFailure(400, errorBody("venuemachine_not_vpin"))
          == SB_SWITCH_MACHINE_TARGET_NOT_VPIN);

    // Before the api sends codes, and for any code it adds later.
    CHECK(switchMachineFailure(404, "") == SB_SWITCH_MACHINE_TARGET_NOT_FOUND);
    CHECK(switchMachineFailure(403, errorBody("something_new"))
          == SB_SWITCH_MACHINE_TARGET_NOT_OWNED);
    CHECK(switchMachineFailure(400, errorBody("required")) == SB_SWITCH_MACHINE_FAILED);
    CHECK(switchMachineFailure(400, errorBody("invalid")) == SB_SWITCH_MACHINE_FAILED);
    CHECK(switchMachineFailure(401, "") == SB_SWITCH_MACHINE_FAILED);

    CHECK(switchMachineFailure(0, "") == SB_SWITCH_MACHINE_NETWORK);
    CHECK(switchMachineFailure(502, errorBody("no_owner")) == SB_SWITCH_MACHINE_NETWORK);
}

TEST_CASE("switchMachineToken reads the reply's token")
{
    CHECK(switchMachineToken(R"({"token":"jwt","channels":["a"]})") == "jwt");
    CHECK(switchMachineToken(R"({"channels":[]})").empty());
    CHECK(switchMachineToken(R"({"token":5})").empty());
    CHECK(switchMachineToken("x").empty());
}

// ---------------------------------------- store ------------------------------------------

TEST_CASE("The table-mappings store reports a change only when the list or result changes")
{
    TableMappingsStore store;
    CHECK(store.snapshot().result == SB_TABLE_MAPPINGS_NONE);
    CHECK(store.etag().empty());

    CHECK(store.apply(200, "\"e1\"", mappingsBody()));
    auto snap = store.snapshot();
    CHECK(snap.result == SB_TABLE_MAPPINGS_OK);
    CHECK(snap.etag == "\"e1\"");
    CHECK(snap.serverTime == "2026-11-02T10:00:00Z");
    REQUIRE(snap.rows.size() == 1);

    CHECK_FALSE(store.apply(200, "\"e1\"", mappingsBody()));
    CHECK_FALSE(store.apply(304, "\"e1\"", ""));

    // A failed read keeps the last good list, and reports the result change once.
    CHECK(store.apply(503, "", ""));
    snap = store.snapshot();
    CHECK(snap.result == SB_TABLE_MAPPINGS_UNAVAILABLE);
    CHECK(snap.rows.size() == 1);
    CHECK(snap.etag == "\"e1\"");
    CHECK_FALSE(store.apply(0, "", ""));

    // The list is still current: back to OK, without a new list.
    CHECK(store.apply(304, "\"e1\"", ""));
    CHECK(store.snapshot().result == SB_TABLE_MAPPINGS_OK);

    CHECK(store.apply(200, "\"e2\"", mappingsBody(CURRENT)));
    CHECK(store.snapshot().rows[0].venueMachine == CURRENT);
}

TEST_CASE("A malformed 200 keeps the last list as unavailable")
{
    TableMappingsStore store;
    REQUIRE(store.apply(200, "\"e1\"", mappingsBody()));
    CHECK(store.apply(200, "\"e2\"", "{}"));
    const auto snap = store.snapshot();
    CHECK(snap.result == SB_TABLE_MAPPINGS_UNAVAILABLE);
    CHECK(snap.etag == "\"e1\"");
    CHECK(snap.rows.size() == 1);
}

TEST_CASE("A not-VPIN or ownerless refusal empties the list and stops reads until resumed")
{
    for (const auto &[code, result] :
         {std::pair {"not_vpin_device", SB_TABLE_MAPPINGS_NOT_VPIN_DEVICE},
          std::pair {"no_owner", SB_TABLE_MAPPINGS_NO_OWNER}}) {
        TableMappingsStore store;
        REQUIRE(store.apply(200, "\"e1\"", mappingsBody()));
        CHECK(store.apply(403, "", errorBody(code)));
        const auto snap = store.snapshot();
        CHECK(snap.result == result);
        CHECK(snap.rows.empty());
        CHECK(snap.etag.empty());
        CHECK(store.suspended());
        store.resume();
        CHECK_FALSE(store.suspended());
    }
}

TEST_CASE("The C accessors read a copy and are safe out of range")
{
    TableMappingsStore store;
    REQUIRE(store.apply(200, "\"e1\"", mappingsBody()));
    auto *m = new sb_table_mappings_struct(store.snapshot());
    CHECK(sb_table_mappings_result(m) == SB_TABLE_MAPPINGS_OK);
    CHECK(std::string {sb_table_mappings_etag(m)} == "\"e1\"");
    CHECK(sb_table_mappings_count(m) == 1);
    CHECK(std::string {sb_table_mapping_key_type(m, 0)} == "script_id");
    CHECK(std::string {sb_table_mapping_table_key(m, 0)} == "abc123");
    CHECK(std::string {sb_table_mapping_venuemachine(m, 0)} == TARGET);
    CHECK(std::string {sb_table_mapping_venuemachine(m, 1)}.empty());
    sb_table_mappings_free(m);

    CHECK(sb_table_mappings_result(nullptr) == SB_TABLE_MAPPINGS_NONE);
    CHECK(sb_table_mappings_count(nullptr) == 0);
    CHECK(std::string {sb_table_mappings_server_time(nullptr)}.empty());
    sb_table_mappings_free(nullptr);
}

// ------------------------------------ machine switch -------------------------------------

namespace {

/// Records the order the switch calls its steps in; each async step can be held and resumed.
struct FakeSwitch {
    std::vector<std::string> calls;
    bool holdDrain {false};
    bool holdAdopt {false};
    bool stop {false};
    int postStatus {200};
    std::string postBody {R"({"token":"jwt"})"};
    MachineSwitch::Done pendingDrain;
    MachineSwitch::Done pendingAdopt;
    std::vector<std::pair<sb_switch_machine_result_t, std::string>> results;

    MachineSwitch make()
    {
        MachineSwitch::Steps steps;
        steps.gate = [this](bool hold) { calls.push_back(hold ? "hold" : "release"); };
        steps.drain = [this](MachineSwitch::Done done) {
            calls.push_back("drain");
            if (holdDrain) {
                pendingDrain = std::move(done);
            } else {
                done();
            }
        };
        steps.post = [this](const std::string &target,
                            std::function<void(int, const std::string &)> reply) {
            calls.push_back("post " + target);
            reply(postStatus, postBody);
        };
        steps.adopt = [this](const std::string &, const std::string &token,
                             MachineSwitch::Done done) {
            calls.push_back("adopt " + token);
            if (holdAdopt) {
                pendingAdopt = std::move(done);
            } else {
                done();
            }
        };
        steps.changed = [this](const std::string &target) { calls.push_back("changed " + target); };
        steps.refetch = [this] { calls.push_back("refetch"); };
        steps.deliver = [this](MachineSwitch::Done fn) {
            calls.push_back("deliver");
            fn();
        };
        steps.stopping = [this] { return stop; };
        return MachineSwitch {std::move(steps)};
    }

    MachineSwitch::Callback callback()
    {
        return [this](sb_switch_machine_result_t result, const std::string &target) {
            results.emplace_back(result, target);
        };
    }
};

} // namespace

TEST_CASE("A switch holds posts, drains, posts, adopts, raises the event, releases, calls back")
{
    FakeSwitch fake;
    auto sw = fake.make();
    sw.start(TARGET, CURRENT, true, fake.callback());

    CHECK(fake.calls
          == std::vector<std::string> {"hold", "drain", std::string {"post "} + TARGET,
                                       "adopt jwt", std::string {"changed "} + TARGET, "release",
                                       "deliver"});
    REQUIRE(fake.results.size() == 1);
    CHECK(fake.results[0].first == SB_SWITCH_MACHINE_OK);
    CHECK(fake.results[0].second == TARGET);
    CHECK_FALSE(sw.inFlight());
}

TEST_CASE("A second switch while one is in flight is BUSY and leaves the first alone")
{
    FakeSwitch fake;
    fake.holdAdopt = true;
    auto sw = fake.make();
    sw.start(TARGET, CURRENT, true, fake.callback());
    REQUIRE(sw.inFlight());

    sw.start(CURRENT, TARGET, true, fake.callback());
    REQUIRE(fake.results.size() == 1);
    CHECK(fake.results[0].first == SB_SWITCH_MACHINE_BUSY);
    CHECK(fake.results[0].second == CURRENT);
    CHECK(sw.inFlight());

    fake.pendingAdopt();
    REQUIRE(fake.results.size() == 2);
    CHECK(fake.results[1].first == SB_SWITCH_MACHINE_OK);
}

TEST_CASE("A switch without a session is refused before anything is held")
{
    FakeSwitch fake;
    auto sw = fake.make();
    sw.start(TARGET, CURRENT, false, fake.callback());
    CHECK(fake.calls == std::vector<std::string> {"deliver"});
    REQUIRE(fake.results.size() == 1);
    CHECK(fake.results[0].first == SB_SWITCH_MACHINE_NOT_AUTHENTICATED);
    CHECK_FALSE(sw.inFlight());
}

TEST_CASE("A switch to the machine already attached changes nothing")
{
    FakeSwitch fake;
    auto sw = fake.make();
    sw.start(TARGET, TARGET, true, fake.callback());
    CHECK(fake.calls
          == std::vector<std::string> {"hold", "drain", std::string {"post "} + TARGET, "release",
                                       "deliver"});
    REQUIRE(fake.results.size() == 1);
    CHECK(fake.results[0].first == SB_SWITCH_MACHINE_ALREADY_ATTACHED);
}

TEST_CASE("A refused switch releases the posts, re-reads the mappings and reports the code")
{
    FakeSwitch fake;
    fake.postStatus = 404;
    fake.postBody = errorBody("venuemachine_not_found");
    auto sw = fake.make();
    sw.start(TARGET, CURRENT, true, fake.callback());
    CHECK(fake.calls
          == std::vector<std::string> {"hold", "drain", std::string {"post "} + TARGET, "release",
                                       "refetch", "deliver"});
    REQUIRE(fake.results.size() == 1);
    CHECK(fake.results[0].first == SB_SWITCH_MACHINE_TARGET_NOT_FOUND);
    CHECK_FALSE(sw.inFlight());
}

TEST_CASE("A 200 without a token is FAILED and adopts nothing")
{
    FakeSwitch fake;
    fake.postBody = R"({"channels":[]})";
    auto sw = fake.make();
    sw.start(TARGET, CURRENT, true, fake.callback());
    REQUIRE(fake.results.size() == 1);
    CHECK(fake.results[0].first == SB_SWITCH_MACHINE_FAILED);
    CHECK(std::find(fake.calls.begin(), fake.calls.end(), "adopt ") == fake.calls.end());
    CHECK(fake.calls.back() == "deliver");
}

TEST_CASE("A switch cut short by shutdown never calls back")
{
    FakeSwitch fake;
    fake.holdDrain = true;
    auto sw = fake.make();
    sw.start(TARGET, CURRENT, true, fake.callback());
    fake.stop = true;
    fake.pendingDrain();
    CHECK(fake.results.empty());
}

TEST_CASE("The callback may start another switch at once")
{
    FakeSwitch fake;
    auto sw = fake.make();
    std::optional<bool> busyInside;
    sw.start(TARGET, CURRENT, true,
             [&](sb_switch_machine_result_t, const std::string &) { busyInside = sw.inFlight(); });
    REQUIRE(busyInside);
    CHECK_FALSE(*busyInside);
}

// ------------------------------------- through Net ---------------------------------------

namespace {

/// An unprovisioned Net (its worker never starts) whose events run on a local io_context.
struct VpinNet {
    boost::asio::io_context io;
    std::vector<EventType> events;
    std::vector<std::string> machineChanged;
    std::unique_ptr<Net> net;

    explicit VpinNet(bool tableMappings = true)
    {
        DeviceInfo info;
        info.vpinTableMappings = tableMappings;
        net = std::make_unique<Net>(std::move(info), std::vector<std::unique_ptr<IKeyResolver>> {});
        NetTestAccess::setEventManager(
                *net, std::make_shared<EventManager>(boost::asio::make_strand(io),
                                                     [this](const EventBase &event) {
                                                         events.push_back(event.type());
                                                         std::string uuid;
                                                         if (Event(&event).getMachineChanged(uuid)) {
                                                             machineChanged.push_back(uuid);
                                                         }
                                                     }));
        NetTestAccess::setStatus(*net, AuthStatus::AuthenticatedPaired);
    }

    void drain() { io.run_for(std::chrono::milliseconds(50)); }
};

} // namespace

TEST_CASE("The mappings read sends the last ETag back and raises one event per change")
{
    VpinNet v;
    std::vector<std::string> sentEtags;
    std::vector<cpr::Response> replies {
            response(200, mappingsBody(), {{"ETag", "\"e1\""}}),
            response(304, "", {{"ETag", "\"e1\""}}),
    };
    std::size_t next = 0;
    NetTestAccess::setVpinTransport(
            *v.net, {.get = [&](const cpr::Url &url, const cpr::Header &header) {
                         CHECK(url.str().find("/table-mappings/") != std::string::npos);
                         const auto it = header.find("If-None-Match");
                         sentEtags.push_back(it == header.end() ? "" : it->second);
                         return replies.at(next++);
                     }});

    NetTestAccess::fetchTableMappingsNow(*v.net);
    NetTestAccess::fetchTableMappingsNow(*v.net);
    v.drain();

    CHECK(sentEtags == std::vector<std::string> {"", "\"e1\""});
    CHECK(v.events == std::vector<EventType> {EventType::TableMappingsUpdated});
    const auto snap = v.net->tableMappings();
    CHECK(snap.result == SB_TABLE_MAPPINGS_OK);
    CHECK(snap.rows.size() == 1);
}

TEST_CASE("A switch through Net posts the target, adopts the token and raises MACHINE_CHANGED")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    std::string postedUrl;
    nlohmann::json postedBody;
    NetTestAccess::setVpinTransport(
            *v.net,
            {.post = [&](const cpr::Url &url, const cpr::Body &body, const cpr::Header &) {
                postedUrl = url.str();
                postedBody = nlohmann::json::parse(body.str());
                return response(200, R"({"token":"jwt","channels":["machine:x"]})");
            }});

    std::string adoptedToken;
    std::vector<sb_switch_machine_result_t> results;
    NetTestAccess::runSwitchMachine(
            *v.net, TARGET,
            [&](sb_switch_machine_result_t result, const std::string &) {
                results.push_back(result);
            },
            [&](const std::string &, const std::string &token, MachineSwitch::Done done) {
                adoptedToken = token;
                CHECK(NetTestAccess::switchGateHeld(*v.net));
                done();
            });
    v.drain();

    CHECK(postedUrl.find("/switch-machine/") != std::string::npos);
    CHECK(postedBody == nlohmann::json {{"venuemachine", TARGET}});
    CHECK(adoptedToken == "jwt");
    CHECK(results == std::vector<sb_switch_machine_result_t> {SB_SWITCH_MACHINE_OK});
    CHECK(v.machineChanged == std::vector<std::string> {TARGET});
    CHECK_FALSE(NetTestAccess::switchGateHeld(*v.net));
}

TEST_CASE("A switch through Net maps the api's refusal and releases the gate")
{
    VpinNet v(false);
    NetTestAccess::setVpinTransport(
            *v.net, {.post = [](const cpr::Url &, const cpr::Body &, const cpr::Header &) {
                return response(403, errorBody("venuemachine_not_owned"));
            }});
    std::vector<sb_switch_machine_result_t> results;
    NetTestAccess::runSwitchMachine(
            *v.net, TARGET,
            [&](sb_switch_machine_result_t result, const std::string &) {
                results.push_back(result);
            },
            [](const std::string &, const std::string &, MachineSwitch::Done) {
                FAIL("a refused switch adopts nothing");
            });
    v.drain();
    CHECK(results == std::vector<sb_switch_machine_result_t> {SB_SWITCH_MACHINE_TARGET_NOT_OWNED});
    CHECK(v.machineChanged.empty());
    CHECK_FALSE(NetTestAccess::switchGateHeld(*v.net));
}

TEST_CASE("A switch through Net before authentication is refused without a request")
{
    VpinNet v(false);
    NetTestAccess::setStatus(*v.net, AuthStatus::NotAuthenticated);
    bool posted = false;
    NetTestAccess::setVpinTransport(
            *v.net, {.post = [&](const cpr::Url &, const cpr::Body &, const cpr::Header &) {
                posted = true;
                return response(200, R"({"token":"jwt"})");
            }});
    std::vector<sb_switch_machine_result_t> results;
    NetTestAccess::runSwitchMachine(
            *v.net, TARGET,
            [&](sb_switch_machine_result_t result, const std::string &) {
                results.push_back(result);
            },
            [](const std::string &, const std::string &, MachineSwitch::Done done) { done(); });
    CHECK_FALSE(posted);
    CHECK(results == std::vector<sb_switch_machine_result_t> {SB_SWITCH_MACHINE_NOT_AUTHENTICATED});
}

TEST_CASE("Session posts for a game after the switch wait for it, in order")
{
    VpinNet v(false);
    std::vector<int> sent;
    NetTestAccess::postOrHoldForSwitch(*v.net, 1, [&] { sent.push_back(1); });
    CHECK(sent == std::vector<int> {1});

    NetTestAccess::holdSwitchGate(*v.net);
    NetTestAccess::postOrHoldForSwitch(*v.net, 1, [&] { sent.push_back(2); });
    NetTestAccess::postOrHoldForSwitch(*v.net, 1, [&] { sent.push_back(3); });
    CHECK(sent == std::vector<int> {1});
    CHECK(NetTestAccess::switchHeldPosts(*v.net) == 2);

    NetTestAccess::releaseSwitchGate(*v.net);
    CHECK(sent == std::vector<int> {1, 2, 3});
    CHECK(NetTestAccess::switchHeldPosts(*v.net) == 0);
}

TEST_CASE("The game from before the switch still settles while the gate is held")
{
    VpinNet v(false);
    GameData finished;
    finished.id = 1;
    v.net->sessionCreate(finished, GameStartOrigin::StartButton, {});

    NetTestAccess::holdSwitchGate(*v.net);
    std::vector<int> sent;
    NetTestAccess::postOrHoldForSwitch(*v.net, 1, [&] { sent.push_back(1); }); // its game end
    NetTestAccess::postOrHoldForSwitch(*v.net, 2, [&] { sent.push_back(2); }); // the next game
    CHECK(sent == std::vector<int> {1});
    CHECK(NetTestAccess::switchHeldPosts(*v.net) == 1);

    NetTestAccess::releaseSwitchGate(*v.net);
    CHECK(sent == std::vector<int> {1, 2});
}

TEST_CASE("A switch while pairing is unresolved is refused without a request")
{
    VpinNet v(false);
    NetTestAccess::setStatus(*v.net, AuthStatus::AuthenticatedCheckingPairing);
    NetTestAccess::setVpinTransport(
            *v.net, {.post = [](const cpr::Url &, const cpr::Body &, const cpr::Header &) {
                FAIL("no request while pairing is unresolved");
                return response(500);
            }});
    std::vector<sb_switch_machine_result_t> results;
    NetTestAccess::runSwitchMachine(
            *v.net, TARGET,
            [&](sb_switch_machine_result_t result, const std::string &) {
                results.push_back(result);
            },
            [](const std::string &, const std::string &, MachineSwitch::Done done) { done(); });
    CHECK(results == std::vector<sb_switch_machine_result_t> {SB_SWITCH_MACHINE_NOT_AUTHENTICATED});
}

TEST_CASE("venueMachine is the scorbitron's machine, not the device")
{
    VpinNet v(false);
    CHECK(v.net->venueMachine().empty());
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    CHECK(v.net->venueMachine() == CURRENT);
    CHECK(v.net->getMachineUuid() != CURRENT);
}

// ---------------------------------------- events -----------------------------------------

TEST_CASE("sb_event_machine_changed reads the new venue machine only from its own event")
{
    const MachineChangedEvent changed {TARGET};
    const char *uuid = nullptr;
    REQUIRE(sb_event_machine_changed(&changed, &uuid));
    CHECK(std::string {uuid} == TARGET);
    CHECK_FALSE(sb_event_machine_changed(&changed, nullptr));

    const TableMappingsUpdatedEvent updated;
    CHECK(updated.type() == EventType::TableMappingsUpdated);
    CHECK_FALSE(sb_event_machine_changed(&updated, &uuid));
}

TEST_CASE("A token fetch begun before a switch does not overwrite the switch's token")
{
    VpinNet v(false);
    NetTestAccess::startTokenRefresh(*v.net, AuthStatus::AuthenticatedPaired, "api-token");
    NetTestAccess::setAuthTransport(*v.net,
                                    {.centrifugoToken =
                                             [&](const std::string &, const std::string &) {
                                                 // The switch adopts its token while this fetch is
                                                 // on the wire.
                                                 NetTestAccess::seedSwitchToken(*v.net,
                                                                                "switch-token");
                                                 return std::string {"stale-socket-token"};
                                             }},
                                    {});
    NetTestAccess::fetchCentrifugoTokenNow(*v.net);
    CHECK(NetTestAccess::cachedCentrifugoToken(*v.net) == "switch-token");

    // A fetch with no switch meanwhile still lands.
    NetTestAccess::setAuthTransport(
            *v.net,
            {.centrifugoToken = [](const std::string &,
                                   const std::string &) { return std::string {"fresh"}; }},
            {});
    NetTestAccess::fetchCentrifugoTokenNow(*v.net);
    CHECK(NetTestAccess::cachedCentrifugoToken(*v.net) == "fresh");
}
