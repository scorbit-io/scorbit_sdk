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
#include <scorbit_sdk/table_mappings.h>
#include <boost/asio/io_context.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
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
    const auto parsed = parseTableMappings(
            nlohmann::json {{"server_time", "t"},
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

    // A new ETag over the same list is kept for the next read, but is not news.
    CHECK_FALSE(store.apply(200, "\"e1b\"", mappingsBody()));
    CHECK(store.etag() == "\"e1b\"");

    // A failed read keeps the last good list, and reports the result change once.
    CHECK(store.apply(503, "", ""));
    snap = store.snapshot();
    CHECK(snap.result == SB_TABLE_MAPPINGS_UNAVAILABLE);
    CHECK(snap.rows.size() == 1);
    CHECK(snap.etag == "\"e1b\"");
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
    /// What resolution reports; nullopt passes the outcome's own result through.
    std::optional<sb_switch_machine_result_t> resolvedAs;
    MachineSwitch::Done pendingDrain;
    MachineSwitch::Done pendingAdopt;
    std::vector<std::pair<sb_switch_machine_result_t, std::string>> results;

    MachineSwitch make()
    {
        using Kind = MachineSwitch::Outcome::Kind;
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
        steps.resolve = [this](const MachineSwitch::Outcome &outcome, const std::string &,
                               const std::string &, MachineSwitch::Resolved done) {
            static const std::map<Kind, std::string> names {{Kind::Switched, "switched"},
                                                            {Kind::AlreadyAttached, "already"},
                                                            {Kind::Refused, "refused"},
                                                            {Kind::Unknown, "unknown"}};
            calls.push_back("resolve " + names.at(outcome.kind));
            done(resolvedAs.value_or(outcome.result));
        };
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

TEST_CASE("A switch holds posts, drains, posts, adopts, resolves, releases, then calls back")
{
    FakeSwitch fake;
    auto sw = fake.make();
    sw.start(TARGET, CURRENT, true, fake.callback());

    CHECK(fake.calls
          == std::vector<std::string> {"hold", "drain", std::string {"post "} + TARGET, "adopt jwt",
                                       "resolve switched", "release", "deliver"});
    REQUIRE(fake.results.size() == 1);
    CHECK(fake.results[0].first == SB_SWITCH_MACHINE_OK);
    CHECK(fake.results[0].second == TARGET);
    CHECK_FALSE(sw.inFlight());
}

TEST_CASE("Every way a switch ends goes through one resolution before posts are released")
{
    struct Row {
        int status;
        std::string body;
        std::string current;
        std::string resolve;
        bool refetch;
    };
    for (const auto &row : std::vector<Row> {
                 {404, errorBody("venuemachine_not_found"), CURRENT, "resolve refused", true},
                 {0, "", CURRENT, "resolve unknown", true},
                 {502, "", CURRENT, "resolve unknown", true},
                 {200, R"({"channels":[]})", CURRENT, "resolve unknown", true},
                 {200, R"({"token":"jwt"})", TARGET, "resolve already", false},
         }) {
        FakeSwitch fake;
        fake.postStatus = row.status;
        fake.postBody = row.body;
        auto sw = fake.make();
        sw.start(TARGET, row.current, true, fake.callback());

        std::vector<std::string> expected {"hold", "drain", std::string {"post "} + TARGET,
                                           row.resolve, "release"};
        if (row.refetch) {
            expected.push_back("refetch");
        }
        expected.push_back("deliver");
        CHECK(fake.calls == expected);
        CHECK(fake.results.size() == 1);
        CHECK_FALSE(sw.inFlight());
    }
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
                *net, std::make_shared<EventManager>(
                              boost::asio::make_strand(io), [this](const EventBase &event) {
                                  events.push_back(event.type());
                                  std::string uuid;
                                  if (Event(&event).getMachineChanged(uuid)) {
                                      machineChanged.push_back(uuid);
                                  }
                              }));
        NetTestAccess::setStatus(*net, AuthStatus::AuthenticatedPaired);
        // As GameState always installs one; this one has no device and finds none.
        NetTestAccess::setProbesManager(*net, std::make_shared<nfc::ProbesManager>(nullptr));
    }

    void drain()
    {
        io.restart(); // a run that found no work stops the context; later drains need it again
        io.run_for(std::chrono::milliseconds(50));
    }
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
            *v.net, {.post = [&](const cpr::Url &url, const cpr::Body &body, const cpr::Header &) {
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
            [&](const std::string &target, const std::string &token, MachineSwitch::Done done) {
                adoptedToken = token;
                CHECK(NetTestAccess::switchGateHeld(*v.net));
                NetTestAccess::adoptForTest(*v.net, target);
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

TEST_CASE("A refresh asked for during a read survives that read's refusal")
{
    VpinNet v;
    std::vector<std::string> urls;
    std::size_t next = 0;
    std::vector<cpr::Response> replies {response(403, errorBody("no_owner")),
                                        response(200, mappingsBody(), {{"ETag", "\"e1\""}})};
    NetTestAccess::setVpinTransport(*v.net, {.get = [&](const cpr::Url &url, const cpr::Header &) {
        urls.push_back(url.str());
        if (next == 0) {
            // The host asks for a refresh while this read is on the wire.
            v.net->refreshTableMappings();
        }
        return replies.at(next++);
    }});

    NetTestAccess::fetchTableMappingsNow(*v.net);
    // The queued re-read went to the (unstarted) worker; run it as the worker would.
    REQUIRE(NetTestAccess::tableMappingsFetchQueued(*v.net));
    NetTestAccess::fetchTableMappingsNow(*v.net);
    v.drain();

    CHECK(urls.size() == 2);
    CHECK(v.net->tableMappings().result == SB_TABLE_MAPPINGS_OK);
}

// ------------------------------------ unknown outcome --------------------------------------

TEST_CASE("scorbitronMachine reads the scorbitron object's machine")
{
    CHECK(scorbitronMachine(R"({"machine":{"id":"vm"}})") == std::optional<std::string> {"vm"});
    CHECK(scorbitronMachine(R"({"machine":null})") == std::optional<std::string> {""});
    CHECK(scorbitronMachine(R"({"shortcode":"x"})") == std::optional<std::string> {""});
    CHECK_FALSE(scorbitronMachine(R"({"machine":{"name":"x"}})"));
    CHECK_FALSE(scorbitronMachine("<html>"));
}

// ------------------------------------- switch resolution table --------------------------------
// Every outcome x every kind of news racing the switch x every server answer. The invariant:
// exactly one callback, the gate released, and a game started during the switch stamped with the
// machine the server ends up with (none if unpaired), never a stale one.

namespace {

constexpr auto OTHER = "9b8a7c6d-5e4f-4a3b-8c2d-1e0f9a8b7c6d";
constexpr auto UNREACHABLE = "<unreachable>";

enum class Race { None, Paired, Unpaired, StaleReply, NewsDuringCheck, NewsEveryCheck };

struct ResolutionRow {
    const char *name;
    int postStatus;
    std::string postBody;
    std::string start;
    Race race;
    std::vector<std::string> answers; ///< what each server check returns, in order
    sb_switch_machine_result_t result;
    std::string machine; ///< the held game's stamp and the device's machine after
    int checks;          ///< server checks made
};

nlohmann::json scorbitronReply(const std::string &machine)
{
    if (machine.empty()) {
        return {{"machine", nullptr}};
    }
    return {{"machine", {{"id", machine}}}};
}

} // namespace

TEST_CASE("Switch resolution: every outcome, race and server answer ends on the server's machine")
{
    const std::string ok = R"({"token":"jwt"})";
    const std::string notOwned = errorBody("venuemachine_not_owned");
    // clang-format off
    const std::vector<ResolutionRow> rows {
        {"switched, clean",                200, ok, CURRENT, Race::None,       {},              SB_SWITCH_MACHINE_OK,                TARGET,  0},
        {"switched, re-pair, on target",   200, ok, CURRENT, Race::Paired,     {TARGET},        SB_SWITCH_MACHINE_OK,                TARGET,  1},
        {"switched, re-pair, elsewhere",   200, ok, CURRENT, Race::Paired,     {OTHER},         SB_SWITCH_MACHINE_FAILED,            OTHER,   1},
        {"switched, unpair, unpaired",     200, ok, CURRENT, Race::Unpaired,   {""},            SB_SWITCH_MACHINE_FAILED,            "",      1},
        {"switched, unpair, on target",    200, ok, CURRENT, Race::Unpaired,   {TARGET},        SB_SWITCH_MACHINE_OK,                TARGET,  1},
        {"switched, stale reply",          200, ok, CURRENT, Race::StaleReply, {TARGET},        SB_SWITCH_MACHINE_OK,                TARGET,  1},
        {"switched, news during check",    200, ok, CURRENT, Race::NewsDuringCheck, {TARGET, OTHER}, SB_SWITCH_MACHINE_FAILED,   OTHER,   2},
        {"switched, news on every check",  200, ok, CURRENT, Race::NewsEveryCheck, {TARGET, TARGET, TARGET}, SB_SWITCH_MACHINE_FAILED, "", 3},
        {"switched, unreachable",          200, ok, CURRENT, Race::Unpaired,   {UNREACHABLE},   SB_SWITCH_MACHINE_FAILED,            "",      1},
        {"already attached, clean",        200, ok, TARGET,  Race::None,       {},              SB_SWITCH_MACHINE_ALREADY_ATTACHED,  TARGET,  0},
        {"already attached, re-pair",      200, ok, TARGET,  Race::Paired,     {OTHER},         SB_SWITCH_MACHINE_FAILED,            OTHER,   1},
        {"refused, clean",                 403, notOwned, CURRENT, Race::None, {},              SB_SWITCH_MACHINE_TARGET_NOT_OWNED,  CURRENT, 0},
        {"refused, re-pair elsewhere",     403, notOwned, CURRENT, Race::Paired, {OTHER},       SB_SWITCH_MACHINE_TARGET_NOT_OWNED,  OTHER,   1},
        {"refused, unpair",                403, notOwned, CURRENT, Race::Unpaired, {""},        SB_SWITCH_MACHINE_TARGET_NOT_OWNED,  "",      1},
        {"no answer, server made it",      502, "", CURRENT, Race::None,       {TARGET},        SB_SWITCH_MACHINE_OK,                TARGET,  1},
        {"no answer, server did not",      502, "", CURRENT, Race::None,       {CURRENT},       SB_SWITCH_MACHINE_NETWORK,           CURRENT, 1},
        {"no answer, unreachable",         502, "", CURRENT, Race::None,       {UNREACHABLE},   SB_SWITCH_MACHINE_NETWORK,           "",      1},
        {"no token, server made it",       200, R"({"channels":[]})", CURRENT, Race::None, {TARGET}, SB_SWITCH_MACHINE_OK,           TARGET,  1},
        {"no token, server did not",       200, R"({"channels":[]})", CURRENT, Race::None, {CURRENT}, SB_SWITCH_MACHINE_FAILED,     CURRENT, 1},
    };
    // clang-format on

    for (const auto &row : rows) {
        DYNAMIC_SECTION(row.name)
        {
            VpinNet v(false);
            NetTestAccess::setMachineUuid(*v.net, row.start);
            std::size_t check = 0;
            NetTestAccess::setVpinTransport(
                    *v.net,
                    {.post =
                             [&](const cpr::Url &, const cpr::Body &, const cpr::Header &) {
                                 // A game starts while the switch is on the wire.
                                 GameData game;
                                 game.id = 1;
                                 v.net->sessionCreate(game, GameStartOrigin::StartButton, {});
                                 switch (row.race) {
                                 case Race::Paired:
                                 case Race::Unpaired:
                                 case Race::NewsDuringCheck:
                                 case Race::NewsEveryCheck:
                                     CHECK(NetTestAccess::foldIntoSwitch(*v.net));
                                     break;
                                 case Race::StaleReply:
                                     NetTestAccess::staleScorbitronReply(*v.net, OTHER);
                                     break;
                                 case Race::None:
                                     break;
                                 }
                                 return response(row.postStatus, row.postBody);
                             },
                     .patch =
                             [&](const cpr::Url &, const cpr::Body &, const cpr::Header &) {
                                 REQUIRE(check < row.answers.size());
                                 const auto answer = row.answers[check++];
                                 if ((row.race == Race::NewsDuringCheck && check == 1)
                                     || row.race == Race::NewsEveryCheck) {
                                     CHECK(NetTestAccess::foldIntoSwitch(*v.net)); // more news
                                 }
                                 if (answer == UNREACHABLE) {
                                     return response(503);
                                 }
                                 return response(200, scorbitronReply(answer).dump());
                             }});

            std::vector<sb_switch_machine_result_t> results;
            NetTestAccess::runSwitchMachine(
                    *v.net, TARGET,
                    [&](sb_switch_machine_result_t result, const std::string &) {
                        results.push_back(result);
                    },
                    [&](const std::string &target, const std::string &, MachineSwitch::Done done) {
                        NetTestAccess::adoptForTest(*v.net, target);
                        done();
                    });
            v.drain();

            CHECK(results == std::vector<sb_switch_machine_result_t> {row.result});
            CHECK(check == static_cast<std::size_t>(row.checks));
            CHECK_FALSE(NetTestAccess::switchGateHeld(*v.net));
            CHECK(v.net->venueMachine() == row.machine);
            // An unreachable server is not an answer, nor is one still overtaken by news at the
            // bound: the pairing stays to be checked (and retried), never guessed or "unpaired".
            const bool unreachable = (!row.answers.empty() && row.answers.back() == UNREACHABLE)
                                  || row.race == Race::NewsEveryCheck;
            const auto expectedStatus = unreachable ? AuthStatus::AuthenticatedCheckingPairing
                                      : row.machine.empty() ? AuthStatus::AuthenticatedUnpaired
                                                            : AuthStatus::AuthenticatedPaired;
            CHECK(NetTestAccess::status(*v.net) == expectedStatus);
            // A game is stamped only with a real machine; without one it publishes live.
            const auto expectedStamp = row.machine.empty()
                                             ? std::optional<std::string> {}
                                             : std::optional<std::string> {row.machine};
            CHECK(NetTestAccess::sessionMachine(*v.net, 1) == expectedStamp);
        }
    }
}

TEST_CASE("A game from before a switch keeps its machine after the switch")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    GameData finished;
    finished.id = 1;
    v.net->sessionCreate(finished, GameStartOrigin::StartButton, {});
    REQUIRE(NetTestAccess::sessionMachine(*v.net, 1) == std::optional<std::string> {CURRENT});

    NetTestAccess::setVpinTransport(
            *v.net, {.post = [](const cpr::Url &, const cpr::Body &, const cpr::Header &) {
                return response(200, R"({"token":"jwt"})");
            }});
    NetTestAccess::runSwitchMachine(
            *v.net, TARGET, [](sb_switch_machine_result_t, const std::string &) {},
            [&](const std::string &target, const std::string &, MachineSwitch::Done done) {
                NetTestAccess::adoptForTest(*v.net, target);
                done();
            });

    CHECK(v.net->venueMachine() == TARGET);
    CHECK(NetTestAccess::sessionMachine(*v.net, 1) == std::optional<std::string> {CURRENT});
}

TEST_CASE("Outside a switch, a scorbitron reply older than an unpair writes nothing")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    const auto gen = NetTestAccess::machineGen(*v.net);
    NetTestAccess::unpairForTest(*v.net); // authoritative: bumps the generation
    NetTestAccess::scorbitronReplyAt(*v.net, CURRENT, gen);
    CHECK(v.net->venueMachine().empty());
}

TEST_CASE("A switch that ends with the pairing unresolved asks for it again")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    int patches = 0;
    NetTestAccess::setVpinTransport(
            *v.net, {.post =
                             [&](const cpr::Url &, const cpr::Body &, const cpr::Header &) {
                                 // A re-authentication during the switch: pairing is being checked
                                 // again.
                                 NetTestAccess::setStatus(*v.net,
                                                          AuthStatus::AuthenticatedCheckingPairing);
                                 return response(200, R"({"token":"jwt"})");
                             },
                     .patch =
                             [&](const cpr::Url &, const cpr::Body &, const cpr::Header &) {
                                 ++patches;
                                 return response(200, scorbitronReply(TARGET).dump());
                             }});
    NetTestAccess::runSwitchMachine(
            *v.net, TARGET, [](sb_switch_machine_result_t, const std::string &) {},
            [&](const std::string &target, const std::string &, MachineSwitch::Done done) {
                NetTestAccess::adoptForTest(*v.net, target);
                NetTestAccess::setStatus(*v.net, AuthStatus::AuthenticatedCheckingPairing);
                done();
            });
    CHECK_FALSE(NetTestAccess::switchGateHeld(*v.net));
    CHECK(NetTestAccess::scorbitronObjectQueued(*v.net)); // the pairing is asked for again
}

TEST_CASE("A switch's reconciling PATCH carries the Net's boot ID", "[boot_id]")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    const auto bootId = NetTestAccess::scorbitronObject(*v.net).at("boot_id");
    std::vector<nlohmann::json> patched;
    NetTestAccess::setVpinTransport(
            *v.net,
            {.post = [](const cpr::Url &, const cpr::Body &,
                        const cpr::Header &) { return response(502); }, // no answer: reconcile
             .patch =
                     [&](const cpr::Url &, const cpr::Body &body, const cpr::Header &) {
                         patched.push_back(nlohmann::json::parse(body.str()));
                         return response(200, scorbitronReply(TARGET).dump());
                     }});
    NetTestAccess::runSwitchMachine(
            *v.net, TARGET, [](sb_switch_machine_result_t, const std::string &) {},
            [&](const std::string &target, const std::string &, MachineSwitch::Done done) {
                NetTestAccess::adoptForTest(*v.net, target);
                done();
            });
    v.drain();

    REQUIRE_FALSE(patched.empty());
    for (const auto &body : patched) {
        CHECK(body.at("boot_id") == bootId);
    }
}

TEST_CASE("A switch that ends during a re-authentication raises no pairing event")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    NetTestAccess::setVpinTransport(
            *v.net, {.post = [](const cpr::Url &, const cpr::Body &, const cpr::Header &) {
                return response(200, R"({"token":"jwt"})");
            }});
    NetTestAccess::runSwitchMachine(
            *v.net, TARGET, [](sb_switch_machine_result_t, const std::string &) {},
            [&](const std::string &target, const std::string &, MachineSwitch::Done done) {
                NetTestAccess::adoptForTest(*v.net, target);
                // A re-authentication starts before the switch concludes; it announces the
                // pairing itself once settled.
                NetTestAccess::setStatus(*v.net, AuthStatus::Authenticating);
                done();
            });
    v.drain();
    CHECK(std::count(v.events.begin(), v.events.end(), EventType::PairingStatusChanged) == 0);
}

TEST_CASE("A reconciliation that fails, then fails again, keeps asking until it succeeds")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    NetTestAccess::setVpinTransport(
            *v.net,
            {.post = [](const cpr::Url &, const cpr::Body &,
                        const cpr::Header &) { return response(502); }, // no answer: unknown
             .patch = [](const cpr::Url &, const cpr::Body &,
                         const cpr::Header &) { return response(503); }}); // reconcile fails
    std::vector<sb_switch_machine_result_t> results;
    NetTestAccess::runSwitchMachine(
            *v.net, TARGET,
            [&](sb_switch_machine_result_t result, const std::string &) {
                results.push_back(result);
            },
            [](const std::string &, const std::string &, MachineSwitch::Done done) { done(); });
    v.drain();

    CHECK(results == std::vector<sb_switch_machine_result_t> {SB_SWITCH_MACHINE_NETWORK});
    CHECK(v.net->venueMachine().empty()); // nothing goes out under a guessed machine
    CHECK(NetTestAccess::status(*v.net) == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(NetTestAccess::scorbitronObjectQueued(*v.net));
    CHECK(std::count(v.events.begin(), v.events.end(), EventType::PairingStatusChanged) == 0);

    // The follow-up PATCH fails too, twice: each failure arms the retry, further out each time.
    const auto first = NetTestAccess::scorbitronRetryBackoff(*v.net);
    NetTestAccess::scorbitronReplyFailed(*v.net);
    CHECK(NetTestAccess::status(*v.net) == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(NetTestAccess::scorbitronRetryBackoff(*v.net) == first * 2);
    NetTestAccess::scorbitronReplyFailed(*v.net);
    CHECK(NetTestAccess::status(*v.net) == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(NetTestAccess::scorbitronRetryBackoff(*v.net) == first * 4);

    // Then it succeeds: the server's answer settles the pairing and the backoff starts over.
    NetTestAccess::scorbitronReplyAt(*v.net, TARGET, NetTestAccess::machineGen(*v.net));
    v.drain();
    CHECK(NetTestAccess::status(*v.net) == AuthStatus::AuthenticatedPaired);
    CHECK(v.net->venueMachine() == TARGET);
    CHECK(NetTestAccess::scorbitronRetryBackoff(*v.net) == first);
    CHECK(NetTestAccess::machineObjectRequests(*v.net) > 0);
    CHECK(std::count(v.events.begin(), v.events.end(), EventType::PairingStatusChanged) == 1);
}

// ----------------------------- side effects of a machine change ---------------------------

TEST_CASE("Any change of machine clears the player profiles")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    const nlohmann::json profiles = nlohmann::json::array({{{JKEY_SCR_POSITION, 1},
                                                            {JKEY_SCR_PLAYER,
                                                             {{JKEY_PLAYER_ID, "p1"},
                                                              {JKEY_PLAYER_USERNAME, "u"},
                                                              {JKEY_PLAYER_DISPLAY_NAME, "n"},
                                                              {JKEY_PLAYER_INITIALS, "N"},
                                                              {JKEY_PLAYER_URL, "x"}}}}});
    auto &players = v.net->playersManager();
    REQUIRE(players.setProfiles(profiles, CURRENT));
    REQUIRE(players.profile(1));

    NetTestAccess::setMachineUuid(*v.net, CURRENT); // the same machine: kept
    CHECK(players.profile(1));
    NetTestAccess::setMachineUuid(*v.net, TARGET); // another machine: gone
    CHECK_FALSE(players.profile(1));

    REQUIRE(players.setProfiles(profiles, TARGET));
    NetTestAccess::unpairForTest(*v.net); // unpaired: gone too
    CHECK_FALSE(players.profile(1));
}

TEST_CASE("A game's reply that outlives a move adds no profiles to the new machine")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    const nlohmann::json scores = nlohmann::json::array({{{JKEY_SCR_POSITION, 1},
                                                          {JKEY_SCR_PLAYER,
                                                           {{JKEY_PLAYER_ID, "p1"},
                                                            {JKEY_PLAYER_USERNAME, "u"},
                                                            {JKEY_PLAYER_DISPLAY_NAME, "n"},
                                                            {JKEY_PLAYER_INITIALS, "N"},
                                                            {JKEY_PLAYER_URL, "x"}}}}});
    GameData game;
    game.id = 1;
    v.net->sessionCreate(game, GameStartOrigin::StartButton, {});
    REQUIRE(NetTestAccess::sessionMachine(*v.net, 1) == std::optional<std::string> {CURRENT});

    // The device moves while the game's reply is still on the wire (past the drain bound).
    NetTestAccess::adoptForTest(*v.net, TARGET);
    NetTestAccess::processScores(*v.net, 1, scores);
    v.drain();
    CHECK_FALSE(v.net->playersManager().profile(1));
    CHECK(std::count(v.events.begin(), v.events.end(), EventType::PlayersUpdated) == 0);

    // A game on the machine the device is on still brings its players.
    GameData next;
    next.id = 2;
    v.net->sessionCreate(next, GameStartOrigin::StartButton, {});
    NetTestAccess::processScores(*v.net, 2, scores);
    v.drain();
    CHECK(v.net->playersManager().profile(1));
}

TEST_CASE("A scorbitron reply whose machine has no id leaves the pairing as it is")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    NetTestAccess::scorbitronRawReply(*v.net, R"({"machine":{"name":"x"}})");
    CHECK(NetTestAccess::status(*v.net) == AuthStatus::AuthenticatedPaired);
    CHECK(v.net->venueMachine() == CURRENT);

    // While the pairing is being checked, it is a failed answer: the retry stays armed.
    NetTestAccess::setStatus(*v.net, AuthStatus::AuthenticatedCheckingPairing);
    const auto first = NetTestAccess::scorbitronRetryBackoff(*v.net);
    NetTestAccess::scorbitronRawReply(*v.net, R"({"machine":{"id":5}})");
    CHECK(NetTestAccess::status(*v.net) == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(NetTestAccess::scorbitronRetryBackoff(*v.net) == first * 2);

    // A null machine is still an authoritative "unpaired".
    NetTestAccess::scorbitronRawReply(*v.net, R"({"machine":null})");
    CHECK(NetTestAccess::status(*v.net) == AuthStatus::AuthenticatedUnpaired);
}

TEST_CASE("A config reply for a machine since left changes nothing")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, CURRENT);
    const auto staleGen = NetTestAccess::machineGen(*v.net);
    NetTestAccess::adoptForTest(*v.net, TARGET); // authoritative: a newer generation
    const auto idBefore = NetTestAccess::deviceMachineId(*v.net);
    const nlohmann::json reply {{JKEY_SCFG_VARIANT_ID, "old-variant"},
                                {JKEY_SCFG_CONFIG, {{JKEY_SCFG_MACHINE_ID, idBefore + 7}}},
                                {JKEY_SCFG_PRICING, {{"free_play", true}}}};

    NetTestAccess::configReplyAt(*v.net, reply.dump(), staleGen);
    v.drain();

    CHECK(NetTestAccess::deviceMachineId(*v.net) == idBefore);
    CHECK(v.events.empty()); // no ConfigReceived, no PricingReceived

    // The same reply at the current generation is applied.
    NetTestAccess::configReplyAt(*v.net, reply.dump(), NetTestAccess::machineGen(*v.net));
    v.drain();
    CHECK(NetTestAccess::deviceMachineId(*v.net) == idBefore + 7);
    CHECK_FALSE(v.events.empty());
}

TEST_CASE("While a switch holds the gate, an agreeing reply still fetches the machine object")
{
    VpinNet v(false);
    NetTestAccess::setMachineUuid(*v.net, TARGET);
    NetTestAccess::holdSwitchGate(*v.net);
    const auto objects = NetTestAccess::machineObjectRequests(*v.net);
    const auto credits = NetTestAccess::creditsStatusRequests(*v.net);

    NetTestAccess::scorbitronReplyAt(*v.net, TARGET, NetTestAccess::machineGen(*v.net));
    CHECK(NetTestAccess::machineObjectRequests(*v.net) == objects + 1);
    CHECK(NetTestAccess::creditsStatusRequests(*v.net) == credits + 1);
    CHECK(v.net->venueMachine() == TARGET);

    // One naming another machine is news for the switch, and fetches nothing.
    NetTestAccess::scorbitronReplyAt(*v.net, OTHER, NetTestAccess::machineGen(*v.net));
    CHECK(NetTestAccess::machineObjectRequests(*v.net) == objects + 1);
    CHECK(v.net->venueMachine() == TARGET);
    NetTestAccess::releaseSwitchGate(*v.net);
}

// ---------------------------------- table-mapping lookup ---------------------------------

namespace {

constexpr auto MAPPING_ID = "3c9e2a71-5d40-4b8f-a1e6-7f2d0c8b9a15";
constexpr auto MACHINE = "9b8a7c6d-5e4f-4a3b-8c2d-1e0f9a8b7c6d";

/// Keys out of sorted order, odd spacing and a number the JSON type would reprint: only a splice
/// sends this as it is.
constexpr auto SIGNATURES = R"([ {"version": 1.50, "digest":"ab12",  "sections": {"script":"s1",
"items":"i1","physics":"p1"}, "procedures":[["Zeta","00ff"], ["Alpha","0a0b"]]} ])";

std::string matchBody(const nlohmann::json &signature)
{
    return nlohmann::json {{"status", "already_mapped"},
                           {"mapping",
                            {{"id", MAPPING_ID},
                             {"key_type", "script_id"},
                             {"table_key", "abc123"},
                             {"venuemachine", TARGET}}},
                           {"venuemachine",
                            {{"id", TARGET},
                             {"name", "Left cabinet"},
                             {"machine", {{"id", MACHINE}, {"name", "Table One"}}}}},
                           {"signature", signature}}
            .dump();
}

nlohmann::json verdict(const nlohmann::json &acceptedVersion)
{
    return {{"status", "modified"},
            {"accepted_version", acceptedVersion},
            {"changed_sections", {"script", "physics"}},
            {"changed_procedures", {"StartGame", "Drain"}}};
}

std::string drfError(const std::string &code, const std::string &attr)
{
    return nlohmann::json {{"type", "validation_error"},
                           {"errors", {{{"code", code}, {"detail", "x"}, {"attr", attr}}}}}
            .dump();
}

TableMatchRequestData request(std::string keyType = "script_id", std::string tableKey = "abc123")
{
    TableMatchRequestData r;
    r.keyType = std::move(keyType);
    r.tableKey = std::move(tableKey);
    return r;
}

/// A VpinNet whose worker runs, so lookups are posted, parked and answered as in production.
struct MatchNet : VpinNet {
    struct Outcome {
        sb_table_match_result_t result {SB_TABLE_MATCH_FAILED};
        TableMatchAnswer answer;
        std::thread::id thread;
    };

    std::mutex mutex;
    std::vector<std::string> posted;
    std::string postedUrl;
    cpr::Response reply;
    std::deque<cpr::Response> script; // answered first, in order, before `reply`

    MatchNet()
        : VpinNet(false) // the table-mappings opt-in does not gate a lookup
    {
        NetTestAccess::setVpinTransport(
                *net,
                {.post = [this](const cpr::Url &url, const cpr::Body &body, const cpr::Header &) {
                    std::scoped_lock lock(mutex);
                    postedUrl = url.str();
                    posted.push_back(body.str());
                    if (!script.empty()) {
                        auto next = script.front();
                        script.pop_front();
                        return next;
                    }
                    return reply;
                }});
        NetTestAccess::startWorker(*net);
    }

    /// The re-authentication a 401 runs inline: the token POST answers @p token. With no signer
    /// (and no resolvers) it fails for good. No retry timer ever fires.
    void stubAuthentication(int token, bool withSigner)
    {
        NetTestAccess::setAuthTransport(
                *net,
                {.noop =
                         [](const cpr::Url &) {
                             // Dated now, or authentication waits for a viable server time
                             char date[64];
                             const auto now = std::time(nullptr);
                             std::tm tm {};
#ifdef _WIN32
                             gmtime_s(&tm, &now);
#else
                             gmtime_r(&now, &tm);
#endif
                             std::strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S GMT", &tm);
                             return response(200, {}, {{"Date", date}});
                         },
                 .token = [token](const cpr::Url &, const cpr::Body &,
                                  const cpr::Header &) { return response(token); },
                 .centrifugoToken = [](const std::string &,
                                       const std::string &) { return std::string {}; },
                 .setSystemTime = [](std::int64_t) { return true; }},
                {.start = [](AuthRetryKind, std::chrono::seconds, detail::task_t) {},
                 .stop = [] {},
                 .random = [] { return 0.0; }});
        if (withSigner) {
            NetTestAccess::setSigner(*net, [](const Digest &) { return Signature {0x01}; });
        }
    }

    ~MatchNet()
    {
        net.reset();
    } // before the transport's members go

    std::size_t postCount()
    {
        std::scoped_lock lock(mutex);
        return posted.size();
    }

    std::future<Outcome> start(TableMatchRequestData r)
    {
        auto promise = std::make_shared<std::promise<Outcome>>();
        net->matchTableMapping(std::move(r), [promise](sb_table_match_result_t result,
                                                       const TableMatchAnswer &answer) {
            promise->set_value({result, answer, std::this_thread::get_id()});
        });
        return promise->get_future();
    }

    Outcome run(TableMatchRequestData r, cpr::Response next)
    {
        {
            std::scoped_lock lock(mutex);
            reply = std::move(next);
        }
        auto outcome = start(std::move(r));
        REQUIRE(outcome.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
        return outcome.get();
    }
};

} // namespace

TEST_CASE("A lookup's errors map by status, then by code")
{
    CHECK(tableMatchFailure(400, errorBody("invalid_table_key")) == SB_TABLE_MATCH_INVALID);
    CHECK(tableMatchFailure(400, "") == SB_TABLE_MATCH_INVALID);
    CHECK(tableMatchFailure(401, "") == SB_TABLE_MATCH_NOT_AUTHENTICATED);
    CHECK(tableMatchFailure(403, errorBody("not_vpin_device")) == SB_TABLE_MATCH_NOT_VPIN_DEVICE);
    CHECK(tableMatchFailure(403, errorBody("no_owner")) == SB_TABLE_MATCH_NO_OWNER);
    CHECK(tableMatchFailure(403, "") == SB_TABLE_MATCH_FAILED);
    CHECK(tableMatchFailure(404, errorBody("unknown_rom")) == SB_TABLE_MATCH_UNKNOWN_ROM);
    CHECK(tableMatchFailure(404, errorBody("unknown_signature"))
          == SB_TABLE_MATCH_UNKNOWN_SIGNATURE);
    CHECK(tableMatchFailure(404, R"({"detail":"Not found."})") == SB_TABLE_MATCH_FAILED);
    CHECK(tableMatchFailure(405, "") == SB_TABLE_MATCH_FAILED);
    CHECK(tableMatchFailure(409, errorBody("venuemachine_needed")) == SB_TABLE_MATCH_NEEDS_MAPPING);
    CHECK(tableMatchFailure(409, errorBody("venuemachine_archived")) == SB_TABLE_MATCH_FAILED);
    CHECK(tableMatchFailure(413, errorBody("signature_too_large"))
          == SB_TABLE_MATCH_SIGNATURE_TOO_LARGE);
    // A proxy's body limit carries no code
    CHECK(tableMatchFailure(413, "<html>413 Request Entity Too Large</html>")
          == SB_TABLE_MATCH_SIGNATURE_TOO_LARGE);
    CHECK(tableMatchFailure(413, "") == SB_TABLE_MATCH_SIGNATURE_TOO_LARGE);
    CHECK(tableMatchFailure(418, "") == SB_TABLE_MATCH_FAILED);
    CHECK(tableMatchFailure(429, "") == SB_TABLE_MATCH_NETWORK);
    CHECK(tableMatchFailure(500, "") == SB_TABLE_MATCH_NETWORK);
    CHECK(tableMatchFailure(503, errorBody("no_owner")) == SB_TABLE_MATCH_NETWORK);
    CHECK(tableMatchFailure(0, "") == SB_TABLE_MATCH_NETWORK);
}

TEST_CASE("apiErrorAttr reads errors[0].attr, empty when null")
{
    CHECK(apiErrorAttr(drfError("invalid_choice", "key_type")) == "key_type");
    CHECK(apiErrorAttr(errorBody("no_owner")).empty()); // attr: null
    CHECK(apiErrorAttr(R"({"code":"x","attr":"top_level_is_not_read"})").empty());
    CHECK(apiErrorAttr("<html>").empty());
}

TEST_CASE("The lookup body splices the caller's signatures and omits what is unset")
{
    auto r = request();
    r.firmwareId = "fw-1";
    r.signaturesJson = SIGNATURES;
    CHECK(tableMatchBody(r)
          == std::string {R"({"firmware_id":"fw-1","key_type":"script_id","table_key":"abc123",)"}
                     + R"("signatures":)" + SIGNATURES + "}");

    auto unavailable = request("rom", "mm_109c");
    unavailable.signatureUnavailable = "table_unreadable_permission";
    CHECK(tableMatchBody(unavailable)
          == R"({"key_type":"rom","signature_unavailable":"table_unreadable_permission",)"
             R"("table_key":"mm_109c"})");

    CHECK(tableMatchBody(request("rom", "mm_109c"))
          == R"({"key_type":"rom","table_key":"mm_109c"})");
}

TEST_CASE("The lookup body is refused for both kinds of evidence or bad signatures")
{
    auto both = request();
    both.signaturesJson = SIGNATURES;
    both.signatureUnavailable = "missing_helper";
    CHECK_FALSE(tableMatchBody(both));

    for (const auto *bad : {"", "not json", "{}", "[]", "[1] trailing", R"({"a":[1]})"}) {
        INFO(bad);
        auto r = request();
        r.signaturesJson = bad;
        CHECK_FALSE(tableMatchBody(r));
    }
}

TEST_CASE("A 200 lookup body fills every field, with or without a verdict")
{
    const auto full = parseTableMatch(matchBody(verdict("2")));
    REQUIRE(full);
    CHECK(full->keyType == "script_id");
    CHECK(full->tableKey == "abc123");
    CHECK(full->mappingId == MAPPING_ID);
    CHECK(full->venueMachine == TARGET);
    CHECK(full->venueMachineName == "Left cabinet");
    CHECK(full->machine == MACHINE);
    CHECK(full->machineName == "Table One");
    CHECK(full->hasVerdict);
    CHECK(full->verdictStatus == "modified");
    CHECK(full->verdictAcceptedVersion == "2");
    CHECK(full->changedSections == std::vector<std::string> {"script", "physics"});
    CHECK(full->changedProcedures == std::vector<std::string> {"StartGame", "Drain"});

    const auto nullVersion = parseTableMatch(matchBody(verdict(nullptr)));
    REQUIRE(nullVersion);
    CHECK(nullVersion->hasVerdict);
    CHECK(nullVersion->verdictAcceptedVersion.empty());

    const auto none = parseTableMatch(matchBody(nullptr));
    REQUIRE(none);
    CHECK_FALSE(none->hasVerdict);
    CHECK(none->verdictStatus.empty());
    CHECK(none->venueMachine == TARGET);
}

TEST_CASE("A malformed 200 lookup body is refused rather than read in part")
{
    CHECK_FALSE(parseTableMatch("<html>"));
    CHECK_FALSE(parseTableMatch(R"({"status":"already_mapped"})"));
    CHECK_FALSE(parseTableMatch(R"({"mapping":{"id":"m","key_type":"rom","table_key":"k"},)"
                                R"("venuemachine":{"name":"no id"}})"));
    CHECK_FALSE(parseTableMatch(R"({"mapping":{"id":"m","key_type":"rom","table_key":"k"},)"
                                R"("venuemachine":{"id":""}})"));
    CHECK_FALSE(parseTableMatch(matchBody("modified")));
    CHECK_FALSE(parseTableMatch(matchBody({{"status", "match"}, {"changed_sections", "script"}})));
}

TEST_CASE("The lookup answer's C accessors and the C++ copy read every field")
{
    auto answer = *parseTableMatch(matchBody(verdict("2")));
    answer.httpStatus = 200;
    auto *m = &answer;
    CHECK(sb_table_match_http_status(m) == 200);
    CHECK(std::string {sb_table_match_venuemachine(m)} == TARGET);
    CHECK(std::string {sb_table_match_machine_name(m)} == "Table One");
    CHECK(sb_table_match_has_verdict(m));
    REQUIRE(sb_table_match_verdict_changed_sections_count(m) == 2);
    CHECK(std::string {sb_table_match_verdict_changed_section(m, 1)} == "physics");
    CHECK(std::string {sb_table_match_verdict_changed_section(m, 2)}.empty());
    CHECK(std::string {sb_table_match_verdict_changed_procedure(m, 0)} == "StartGame");
    CHECK(std::string {sb_table_match_verdict_changed_procedure(m, 2)}.empty());

    const auto copy = TableMatch::fromC(m);
    CHECK(copy.httpStatus == 200);
    CHECK(copy.keyType == "script_id");
    CHECK(copy.tableKey == "abc123");
    CHECK(copy.mappingId == MAPPING_ID);
    CHECK(copy.venueMachine == TARGET);
    CHECK(copy.venueMachineName == "Left cabinet");
    CHECK(copy.machine == MACHINE);
    CHECK(copy.machineName == "Table One");
    REQUIRE(copy.hasVerdict);
    CHECK(copy.verdict.status == "modified");
    CHECK(copy.verdict.acceptedVersion == "2");
    CHECK(copy.verdict.changedSections == std::vector<std::string> {"script", "physics"});
    CHECK(copy.verdict.changedProcedures == std::vector<std::string> {"StartGame", "Drain"});

    CHECK_FALSE(TableMatch::fromC(nullptr).hasVerdict);

    sb_table_match_request_t r = sb_table_match_request_create();
    sb_table_match_request_set_key(r, "rom", "mm_109c");
    sb_table_match_request_set_firmware_id(r, "fw-1");
    sb_table_match_request_set_signatures_json(r, SIGNATURES);
    sb_table_match_request_set_signatures_json(r, nullptr);
    sb_table_match_request_set_signature_unavailable(r, "");
    CHECK(r->keyType == "rom");
    CHECK(r->tableKey == "mm_109c");
    CHECK(r->firmwareId == "fw-1");
    CHECK_FALSE(r->signaturesJson);
    CHECK(r->signatureUnavailable == std::string {});
    sb_table_match_request_free(r);
}

TEST_CASE("A lookup through Net posts the request and reads a 200 into every field")
{
    MatchNet n;
    auto r = request();
    r.firmwareId = "fw-1";
    r.signaturesJson = SIGNATURES;
    const auto out = n.run(std::move(r), response(200, matchBody(verdict("2"))));

    CHECK(out.result == SB_TABLE_MATCH_MAPPED);
    CHECK(out.thread != std::this_thread::get_id());
    CHECK(out.answer.httpStatus == 200);
    CHECK(out.answer.errorCode.empty());
    CHECK(out.answer.venueMachine == TARGET);
    CHECK(out.answer.machine == MACHINE);
    CHECK(out.answer.verdictStatus == "modified");
    CHECK(n.postedUrl.find("/api/v2/scorbitrons/") != std::string::npos);
    CHECK(n.postedUrl.find("/table-mappings/") != std::string::npos);
    REQUIRE(n.posted.size() == 1);
    // The caller's signatures text arrives byte for byte, after the other fields.
    CHECK(n.posted[0]
          == std::string {R"({"firmware_id":"fw-1","key_type":"script_id","table_key":"abc123",)"}
                     + R"("signatures":)" + SIGNATURES + "}");

    // No side effects: no event, and the mappings snapshot is untouched.
    n.drain();
    CHECK(n.events.empty());
    CHECK(n.net->tableMappings().result == SB_TABLE_MAPPINGS_NONE);
}

TEST_CASE("A lookup through Net maps every answer and keeps the api's code and attr")
{
    struct Row {
        const char *name;
        int status;
        std::string body;
        sb_table_match_result_t expected;
        std::string code;
        std::string attr;
    };
    const std::vector<Row> rows {
            {"400 invalid_table_key", 400, errorBody("invalid_table_key"), SB_TABLE_MATCH_INVALID,
             "invalid_table_key", ""},
            {"400 invalid_signature", 400, drfError("invalid_signature", "signatures"),
             SB_TABLE_MATCH_INVALID, "invalid_signature", "signatures"},
            {"400 signature_conflict", 400, drfError("signature_conflict", "signatures"),
             SB_TABLE_MATCH_INVALID, "signature_conflict", "signatures"},
            {"400 required", 400, drfError("required", "table_key"), SB_TABLE_MATCH_INVALID,
             "required", "table_key"},
            {"400 invalid_choice on key_type", 400, drfError("invalid_choice", "key_type"),
             SB_TABLE_MATCH_INVALID, "invalid_choice", "key_type"},
            {"403 not_vpin_device", 403, errorBody("not_vpin_device"),
             SB_TABLE_MATCH_NOT_VPIN_DEVICE, "not_vpin_device", ""},
            {"403 no_owner", 403, errorBody("no_owner"), SB_TABLE_MATCH_NO_OWNER, "no_owner", ""},
            {"404 unknown_rom", 404, errorBody("unknown_rom"), SB_TABLE_MATCH_UNKNOWN_ROM,
             "unknown_rom", ""},
            {"404 unknown_signature", 404, errorBody("unknown_signature"),
             SB_TABLE_MATCH_UNKNOWN_SIGNATURE, "unknown_signature", ""},
            {"404 with no code", 404, R"({"detail":"Not found."})", SB_TABLE_MATCH_FAILED, "", ""},
            {"405 with no code", 405, R"({"detail":"Method not allowed."})", SB_TABLE_MATCH_FAILED,
             "", ""},
            {"409 venuemachine_needed", 409, errorBody("venuemachine_needed"),
             SB_TABLE_MATCH_NEEDS_MAPPING, "venuemachine_needed", ""},
            {"409 other", 409, errorBody("venuemachine_archived"), SB_TABLE_MATCH_FAILED,
             "venuemachine_archived", ""},
            {"413 signature_too_large", 413, errorBody("signature_too_large"),
             SB_TABLE_MATCH_SIGNATURE_TOO_LARGE, "signature_too_large", ""},
            {"429", 429, R"({"code":"throttled"})", SB_TABLE_MATCH_NETWORK, "throttled", ""},
            {"500", 500, "<html>", SB_TABLE_MATCH_NETWORK, "", ""},
            {"503", 503, "", SB_TABLE_MATCH_NETWORK, "", ""},
            {"an unknown 4xx", 418, errorBody("teapot"), SB_TABLE_MATCH_FAILED, "teapot", ""},
            {"a malformed 200", 200, R"({"status":"already_mapped"})", SB_TABLE_MATCH_FAILED, "",
             ""},
    };

    MatchNet n;
    for (const auto &row : rows) {
        INFO(row.name);
        const auto out = n.run(request(), response(row.status, row.body));
        CHECK(out.result == row.expected);
        CHECK(out.answer.httpStatus == row.status);
        CHECK(out.answer.errorCode == row.code);
        CHECK(out.answer.errorAttr == row.attr);
        CHECK(out.answer.venueMachine.empty());
        CHECK_FALSE(out.answer.hasVerdict);
    }
    // One request per call: none is retried, cached or folded into another.
    CHECK(n.postCount() == rows.size());
}

TEST_CASE("A lookup that gets no response is NETWORK with no status")
{
    MatchNet n;
    const auto out = n.run(request(), response(0));
    CHECK(out.result == SB_TABLE_MATCH_NETWORK);
    CHECK(out.answer.httpStatus == 0);
    CHECK(out.answer.errorCode.empty());
    // One request: the server may have acted on one it never answered
    CHECK(n.postCount() == 1);
}

TEST_CASE("A lookup whose 401 leaves authentication pending waits for it, then is sent again")
{
    MatchNet n;
    n.stubAuthentication(503, true); // the re-authentication fails, for now
    {
        std::scoped_lock lock(n.mutex);
        n.script = {response(401, errorBody("not_authenticated"))};
        n.reply = response(200, matchBody(nullptr));
    }
    auto outcome = n.start(request());
    CHECK(outcome.wait_for(std::chrono::milliseconds(500)) == std::future_status::timeout);
    CHECK(n.postCount() == 1);
    REQUIRE(NetTestAccess::status(*n.net) == AuthStatus::NotAuthenticated);

    NetTestAccess::setStatusAndNotify(*n.net, AuthStatus::AuthenticatedPaired);
    REQUIRE(outcome.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK(outcome.get().result == SB_TABLE_MATCH_MAPPED);
    CHECK(n.postCount() == 2);
}

TEST_CASE("A lookup keeps waiting through a second 401 while its first wait has not expired")
{
    MatchNet n;
    n.stubAuthentication(503, true); // every re-authentication fails, for now
    {
        std::scoped_lock lock(n.mutex);
        n.script = {response(401, errorBody("not_authenticated")),
                    response(401, errorBody("not_authenticated"))};
        n.reply = response(200, matchBody(nullptr));
    }
    auto outcome = n.start(request());
    CHECK(outcome.wait_for(std::chrono::milliseconds(500)) == std::future_status::timeout);
    REQUIRE(n.postCount() == 1);

    // Authentication completes; the lookup is sent again and meets a second 401
    NetTestAccess::setStatusAndNotify(*n.net, AuthStatus::AuthenticatedPaired);
    CHECK(outcome.wait_for(std::chrono::milliseconds(500)) == std::future_status::timeout);
    REQUIRE(n.postCount() == 2);
    REQUIRE(NetTestAccess::status(*n.net) == AuthStatus::NotAuthenticated);

    NetTestAccess::setStatusAndNotify(*n.net, AuthStatus::AuthenticatedPaired);
    REQUIRE(outcome.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK(outcome.get().result == SB_TABLE_MATCH_MAPPED);
    CHECK(n.postCount() == 3);
}

TEST_CASE("A lookup whose 401 ends authentication for good keeps the api's code and attr")
{
    MatchNet n;
    n.stubAuthentication(503, false); // no key at all: it fails for good
    {
        std::scoped_lock lock(n.mutex);
        n.reply = response(
                401, nlohmann::json {{"type", "client_error"},
                                     {"errors",
                                      {{{"code", "token_not_valid"}, {"attr", "authorization"}}}}}
                             .dump());
    }
    auto outcome = n.start(request());
    REQUIRE(outcome.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    const auto out = outcome.get();
    CHECK(out.result == SB_TABLE_MATCH_NOT_AUTHENTICATED);
    CHECK(out.answer.httpStatus == 401);
    CHECK(out.answer.errorCode == "token_not_valid");
    CHECK(out.answer.errorAttr == "authorization");
    CHECK(n.postCount() == 1);
}

TEST_CASE("A lookup sends only the evidence it was given")
{
    MatchNet n;
    auto unavailable = request("script_id", "abc123");
    unavailable.signatureUnavailable = "legacy_protected";
    n.run(std::move(unavailable), response(409, errorBody("venuemachine_needed")));
    n.run(request("rom", "mm_109c"), response(404, errorBody("unknown_rom")));

    REQUIRE(n.posted.size() == 2);
    CHECK(nlohmann::json::parse(n.posted[0])
          == nlohmann::json {{"key_type", "script_id"},
                             {"table_key", "abc123"},
                             {"signature_unavailable", "legacy_protected"}});
    CHECK(nlohmann::json::parse(n.posted[1])
          == nlohmann::json {{"key_type", "rom"}, {"table_key", "mm_109c"}});
}

TEST_CASE("A refused lookup sends nothing and is answered on the worker")
{
    MatchNet n;
    std::vector<TableMatchRequestData> refused;
    auto both = request();
    both.signaturesJson = SIGNATURES;
    both.signatureUnavailable = "missing_helper";
    refused.push_back(both);
    for (const auto *bad : {"not json", R"({"version":1})", "[]", ""}) {
        auto r = request();
        r.signaturesJson = bad;
        refused.push_back(r);
    }

    for (auto &r : refused) {
        const auto out = n.run(std::move(r), response(200, matchBody(nullptr)));
        CHECK(out.result == SB_TABLE_MATCH_INVALID);
        CHECK(out.answer.httpStatus == 0);
        CHECK(out.thread != std::this_thread::get_id());
    }
    CHECK(n.postCount() == 0);
}

TEST_CASE("A lookup before authentication waits for it, then is sent")
{
    MatchNet n;
    NetTestAccess::setStatus(*n.net, AuthStatus::NotAuthenticated);
    {
        std::scoped_lock lock(n.mutex);
        n.reply = response(200, matchBody(nullptr));
    }
    auto outcome = n.start(request());
    CHECK(outcome.wait_for(std::chrono::milliseconds(200)) == std::future_status::timeout);
    CHECK(n.postCount() == 0);

    NetTestAccess::setStatusAndNotify(*n.net, AuthStatus::AuthenticatedCheckingPairing);
    REQUIRE(outcome.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    CHECK(outcome.get().result == SB_TABLE_MATCH_MAPPED);
    CHECK(n.postCount() == 1);
}

TEST_CASE("A lookup waiting for authentication is NOT_AUTHENTICATED when it fails for good")
{
    MatchNet n;
    NetTestAccess::setStatus(*n.net, AuthStatus::Authenticating);
    auto outcome = n.start(request());
    CHECK(outcome.wait_for(std::chrono::milliseconds(200)) == std::future_status::timeout);

    NetTestAccess::setStatusAndNotify(*n.net, AuthStatus::AuthenticationFailed);
    REQUIRE(outcome.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    const auto out = outcome.get();
    CHECK(out.result == SB_TABLE_MATCH_NOT_AUTHENTICATED);
    CHECK(out.answer.httpStatus == 0);
    CHECK(n.postCount() == 0);
}
