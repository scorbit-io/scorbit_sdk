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

#include "net.h"
#include "net_util.h"
#include <scorbit_sdk/scorbit_sdk.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/filesystem.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;
using boost::asio::ip::tcp;

// Using this indirection to mock free function
struct {
    MAKE_MOCK1(signer, Signature(const Digest &digest));
} Signer;

Signature signer(const Digest &digest)
{
    return Signer.signer(digest);
}

TEST_CASE("Net constructor")
{
    DeviceInfo info;
    Net net {std::move(info), {}};
}

TEST_CASE("Net hostname")
{
    DeviceInfo info;
    Net net {std::move(info), {}};
    CHECK(net.hostname() == "https://api.scorbit.io:443"); // By default it's production

    net.setHostname("production");
    CHECK(net.hostname() == "https://api.scorbit.io:443");
    CHECK(net.cfHostname() == "wss://sws.scorbit.io:443");

    net.setHostname("staging");
    CHECK(net.hostname() == "https://staging.scorbit.io:443");
    CHECK(net.cfHostname() == "wss://sws-staging.scorbit.io:443");

    net.setHostname("");
    CHECK(net.hostname() == "https://api.scorbit.io:443");
    CHECK(net.cfHostname() == "wss://sws.scorbit.io:443");

    net.setHostname("http://localhost:8080", "ws://localhost:9000");
    CHECK(net.hostname() == "http://localhost:8080");
    CHECK(net.cfHostname() == "ws://localhost:9000");

    // Make sure that anything after port is thrown away
    net.setHostname("http://localhost:8080/api");
    CHECK(net.hostname() == "http://localhost:8080");
    CHECK(net.cfHostname() == "ws://localhost:8080");

    net.setHostname("https://example.com/api");
    CHECK(net.hostname() == "https://example.com:443");
    CHECK(net.cfHostname() == "wss://example.com:443");
}

TEST_CASE("getSignature calls signer with correct digest")
{
    using trompeloeil::_;

    const auto uuid = "a61c63823b4b42ccbad2eb7bcd831a0d";
    const auto timestamp = "1729086480";

    /*
        import hashlib
        uuid = 'a61c63823b4b42ccbad2eb7bcd831a0d'
        timestamp_str = "1729086480"
        nonce = bytes.fromhex(uuid) + bytes(timestamp_str, 'utf-8')
        sha256 = hashlib.sha256(nonce)
        print('SHA256 hash: ' + sha256.hexdigest())
    */

    // Pre-calculated digest which is SHA256 of the concatenated UUID and timestamp using
    // python script above
    Digest expectedDigest = {0x8f, 0xcb, 0xbb, 0x3e, 0x2e, 0x1f, 0x9c, 0x3b, 0x74, 0xfc, 0x64,
                             0x3b, 0xd4, 0xb0, 0xec, 0xd5, 0xd1, 0x99, 0xb6, 0x3a, 0x65, 0xab,
                             0x98, 0xb5, 0xdf, 0x89, 0xab, 0x6b, 0xf2, 0xa6, 0xd5, 0xb7};

    // Expect the mock signer to be called with the correct digest
    REQUIRE_CALL(Signer, signer(_)).WITH(_1 == expectedDigest).RETURN(Signature {}).TIMES(1);

    // Invoke the method
    getSignature(signer, uuid, timestamp);
}

// ---------------------------------------------------------------------------------------------
// SB-4795: typed config update, diagnostics request-generation echo, and HTTP status reporting.
// ---------------------------------------------------------------------------------------------

namespace {

cpr::Response makeResponse(int statusCode, std::string body = {})
{
    cpr::Response r;
    r.status_code = statusCode;
    r.text = std::move(body);
    return r;
}

/// A transport that replays a fixed script of responses and counts how many times it was called.
struct ScriptedTransport {
    std::vector<cpr::Response> script;
    int calls = 0;
    std::string lastUrl;
    std::string lastBody;

    cpr::Response operator()(const cpr::Url &url, const cpr::Body &body, const cpr::Header &,
                             const cpr::Timeout &, bool)
    {
        lastUrl = url.str();
        lastBody = body.str();
        const auto index = std::min<size_t>(static_cast<size_t>(calls), script.size() - 1);
        ++calls;
        return script[index];
    }
};

} // namespace

// --------------------------- A: typed config update payload -----------------------------------

TEST_CASE("buildConfigUpdatePayload sends a blank version verbatim")
{
    const auto payload = buildConfigUpdatePayload("capture_retained", "", true, std::nullopt);

    // The literal wire form matters: a blank version is how a caller withdraws a prior report, so
    // it must be present and empty, never omitted and never null.
    CHECK(payload.find("\"version\":\"\"") != std::string::npos);

    const auto j = nlohmann::json::parse(payload);
    REQUIRE(j.contains("version"));
    CHECK(j["version"].is_string());
    CHECK(j["version"].get<std::string>().empty());
}

TEST_CASE("buildConfigUpdatePayload passes an unknown type through unchanged")
{
    // Nothing validates the type against a list; a type the SDK has never seen reaches the wire
    // exactly as given.
    const auto type = "a_type_the_sdk_has_never_heard_of";
    const auto j =
            nlohmann::json::parse(buildConfigUpdatePayload(type, "1.2.3", true, std::nullopt));

    CHECK(j["type"].get<std::string>() == type);
    CHECK(j["version"].get<std::string>() == "1.2.3");
    CHECK(j["installed"].get<bool>() == true);
}

TEST_CASE("buildConfigUpdatePayload omits the log unless one is supplied")
{
    const auto without =
            nlohmann::json::parse(buildConfigUpdatePayload("sdk", "1.0.0", false, std::nullopt));
    CHECK_FALSE(without.contains("log"));
    CHECK(without["installed"].get<bool>() == false);

    // An empty string is a caller-supplied empty log, which is distinct from no log at all.
    const auto withEmpty = nlohmann::json::parse(
            buildConfigUpdatePayload("sdk", "1.0.0", true, std::optional<std::string> {""}));
    REQUIRE(withEmpty.contains("log"));
    CHECK(withEmpty["log"].get<std::string>().empty());

    const auto withText = nlohmann::json::parse(
            buildConfigUpdatePayload("sdk", "1.0.0", true, std::optional<std::string> {"oops"}));
    CHECK(withText["log"].get<std::string>() == "oops");
}

// --------------------------- B: diagnostics request-generation echo ---------------------------

TEST_CASE("buildDiagnosticsMultipart omits request_generation when none is supplied")
{
    const auto parts = buildDiagnosticsMultipart("/tmp/diagnostics.tar.gz", std::nullopt);

    REQUIRE(parts.parts.size() == 1);
    CHECK(parts.parts[0].name == "file");

    const auto hasGeneration =
            std::any_of(parts.parts.begin(), parts.parts.end(),
                        [](const cpr::Part &p) { return p.name == "request_generation"; });
    CHECK_FALSE(hasGeneration);
}

TEST_CASE("buildDiagnosticsMultipart emits request_generation as a decimal string")
{
    const auto parts =
            buildDiagnosticsMultipart("/tmp/diagnostics.tar.gz", std::optional<std::uint64_t> {42});

    REQUIRE(parts.parts.size() == 2);
    CHECK(parts.parts[0].name == "file");
    CHECK(parts.parts[1].name == "request_generation");
    CHECK(parts.parts[1].value == "42");
}

TEST_CASE("buildDiagnosticsMultipart emits a zero generation rather than dropping it")
{
    // 0 is a legitimate generation; only an absent optional omits the field.
    const auto parts =
            buildDiagnosticsMultipart("/tmp/diagnostics.tar.gz", std::optional<std::uint64_t> {0});

    REQUIRE(parts.parts.size() == 2);
    CHECK(parts.parts[1].name == "request_generation");
    CHECK(parts.parts[1].value == "0");
}

// --------------------------- C1: HTTP status reporting ----------------------------------------

TEST_CASE("HttpStatusCallback reports the status of the final attempt and not the first")
{
    DeviceInfo info;
    Net net {std::move(info), {}};

    // A transport failure (status 0) is retried; the 400 that follows is the final attempt and is
    // what the caller must see. This is the case that fails if the status is read from a variable
    // scoped inside the retry loop.
    ScriptedTransport transport {{makeResponse(0), makeResponse(400, R"({"detail":"nope"})")}};

    Error seenError {Error::Success};
    int seenStatus = -1;
    std::string seenReply;

    HttpStatusCallback callback = [&](Error error, int httpStatus, const std::string &reply) {
        seenError = error;
        seenStatus = httpStatus;
        seenReply = reply;
    };

    NetTestAccess::request(net, std::move(callback), std::ref(transport), "{}")();

    CHECK(transport.calls == 2);
    CHECK(seenStatus == 400);
    CHECK(seenError == Error::ApiError);
    CHECK(seenReply == R"({"detail":"nope"})");
}

TEST_CASE("A 4xx is reported and not retried")
{
    DeviceInfo info;
    Net net {std::move(info), {}};

    ScriptedTransport transport {{makeResponse(400, R"({"detail":"Unknown update type: x"})")}};

    int seenStatus = -1;
    Error seenError {Error::Success};
    HttpStatusCallback callback = [&](Error error, int httpStatus, const std::string &) {
        seenError = error;
        seenStatus = httpStatus;
    };

    NetTestAccess::request(net, std::move(callback), std::ref(transport), "{}")();

    CHECK(seenStatus == 400);
    CHECK(seenError == Error::ApiError);
    CHECK(transport.calls == 1); // the SDK must not retry a 4xx
}

TEST_CASE("A 401 is retried unlike every other 4xx")
{
    DeviceInfo info;
    Net net {std::move(info), {}};

    // 401 is the documented exception to "a 4xx is not retried": the SDK owns authentication, so
    // it re-authenticates and tries again rather than handing the status back. The caller sees the
    // status of the attempt that followed.
    ScriptedTransport transport {{makeResponse(401), makeResponse(400, "still no")}};

    int seenStatus = -1;
    HttpStatusCallback callback = [&](Error, int httpStatus, const std::string &) {
        seenStatus = httpStatus;
    };

    NetTestAccess::request(net, std::move(callback), std::ref(transport), "{}")();

    CHECK(transport.calls == 2); // retried, where a 400 or 500 would have stopped at 1
    CHECK(seenStatus == 400);
}

TEST_CASE("A 401 that never clears stops at the retry limit")
{
    DeviceInfo info;
    Net net {std::move(info), {}};

    // The 401 retry is bounded by NUM_RETRIES; it does not spin.
    ScriptedTransport transport {{makeResponse(401)}};

    int seenStatus = -1;
    Error seenError {Error::Success};
    HttpStatusCallback callback = [&](Error error, int httpStatus, const std::string &) {
        seenError = error;
        seenStatus = httpStatus;
    };

    NetTestAccess::request(net, std::move(callback), std::ref(transport), "{}")();

    CHECK(transport.calls == 3);
    CHECK(seenStatus == 401);
    CHECK(seenError == Error::ApiError);
}

TEST_CASE("A 5xx is reported and not retried")
{
    DeviceInfo info;
    Net net {std::move(info), {}};

    ScriptedTransport transport {{makeResponse(500, "boom")}};

    int seenStatus = -1;
    HttpStatusCallback callback = [&](Error, int httpStatus, const std::string &) {
        seenStatus = httpStatus;
    };

    NetTestAccess::request(net, std::move(callback), std::ref(transport), "{}")();

    CHECK(seenStatus == 500);
    CHECK(transport.calls == 1); // the SDK must not retry a 5xx either
}

TEST_CASE("A 2xx reports its status alongside success")
{
    DeviceInfo info;
    Net net {std::move(info), {}};

    ScriptedTransport transport {{makeResponse(204)}};

    int seenStatus = -1;
    Error seenError {Error::ApiError};
    HttpStatusCallback callback = [&](Error error, int httpStatus, const std::string &) {
        seenError = error;
        seenStatus = httpStatus;
    };

    NetTestAccess::request(net, std::move(callback), std::ref(transport), "{}")();

    CHECK(seenStatus == 204);
    CHECK(seenError == Error::Success);
    CHECK(transport.calls == 1);
}

TEST_CASE("StringCallback callers are unaffected by the status-carrying path")
{
    DeviceInfo info;
    Net net {std::move(info), {}};

    ScriptedTransport transport {{makeResponse(400, "rejected")}};

    // The existing two-argument callback must still compile and still be invoked; the status is
    // simply discarded for it.
    Error seenError {Error::Success};
    std::string seenReply;
    StringCallback callback = [&](Error error, const std::string &reply) {
        seenError = error;
        seenReply = reply;
    };

    NetTestAccess::request(net, std::move(callback), std::ref(transport), "{}")();

    CHECK(seenError == Error::ApiError);
    CHECK(seenReply == "rejected");
    CHECK(transport.calls == 1);
}

TEST_CASE("A signer-failure retry re-arms a failed authentication")
{
    // A normal authentication only starts from NotAuthenticated. Left at AuthenticationFailed,
    // the retry scheduled after an empty signature would be turned away and never authenticate.
    Net net {DeviceInfo {}, {}};

    CHECK(NetTestAccess::rearmAuthAfterFailure(net, AuthStatus::AuthenticationFailed));
    CHECK(NetTestAccess::status(net) == AuthStatus::NotAuthenticated);
}

TEST_CASE("A signer-failure retry leaves an authentication that has moved on alone")
{
    for (const auto moved : {AuthStatus::NotAuthenticated, AuthStatus::Authenticating,
                             AuthStatus::AuthenticatedCheckingPairing,
                             AuthStatus::AuthenticatedUnpaired, AuthStatus::AuthenticatedPaired}) {
        Net net {DeviceInfo {}, {}};
        CAPTURE(static_cast<int>(moved));
        CHECK_FALSE(NetTestAccess::rearmAuthAfterFailure(net, moved));
        CHECK(NetTestAccess::status(net) == moved);
    }
}

// --------------------------- D: hardware probe (firmware_probe) -------------------------------

namespace {

constexpr auto PROBE_RUN_ID = "3f2b8c1e-8d4a-4a57-9a3e-2c6f1d0b7e55";
constexpr auto PROBE_REPLY_TO =
        "/internal/api/diagnostics/hardware-probe-result/3f2b8c1e-8d4a-4a57-9a3e-2c6f1d0b7e55/";

/// A Net whose events run on a local io_context: an unprovisioned Net never starts its worker.
/// io is declared first so it outlives the net's event manager, which holds a strand on it.
struct ProbeNet {
    boost::asio::io_context io;
    std::vector<std::pair<std::string, bool>> received;
    Net net {DeviceInfo {}, {}};

    ProbeNet()
    {
        auto onEvent = [this](const EventBase &event) {
            std::string runId;
            bool force = false;
            if (Event(&event).eventHardwareProbeRequested(runId, force)) {
                received.emplace_back(runId, force);
            }
        };
        NetTestAccess::setEventManager(
                net, std::make_shared<EventManager>(boost::asio::make_strand(io), onEvent));
    }

    void handle(const nlohmann::json &payload) { NetTestAccess::handleHardwareProbe(net, payload); }
    void drain() { io.run_for(std::chrono::milliseconds(50)); }
};

nlohmann::json probePayload(std::string replyTo = PROBE_REPLY_TO, bool force = false)
{
    return {{"run_id", PROBE_RUN_ID}, {"reply_to", std::move(replyTo)}, {"force_re_detect", force}};
}

} // namespace

TEST_CASE("firmware_probe raises HardwareProbeRequested with run_id and force_re_detect")
{
    ProbeNet probe;

    probe.handle(probePayload(PROBE_REPLY_TO, true));
    probe.drain();

    REQUIRE(probe.received.size() == 1);
    CHECK(probe.received[0].first == PROBE_RUN_ID);
    CHECK(probe.received[0].second);
}

TEST_CASE("firmware_probe accepts reply_to without the leading slash")
{
    ProbeNet probe;

    probe.handle(probePayload(std::string {PROBE_REPLY_TO}.substr(1)));
    probe.drain();

    REQUIRE(probe.received.size() == 1);
    CHECK_FALSE(probe.received[0].second);
}

TEST_CASE("firmware_probe with a mismatched reply_to raises no event")
{
    for (const std::string replyTo :
         {"", "https://evil.example/internal/api/diagnostics/hardware-probe-result/x/",
          "/internal/api/diagnostics/hardware-probe-result/other-run/",
          "/internal/api/diagnostics/wifi-event/3f2b8c1e-8d4a-4a57-9a3e-2c6f1d0b7e55/"}) {
        CAPTURE(replyTo);
        ProbeNet probe;

        probe.handle(probePayload(replyTo));
        probe.drain();

        CHECK(probe.received.empty());
    }
}

TEST_CASE("firmware_probe redelivered with the same run_id raises one event")
{
    ProbeNet probe;

    probe.handle(probePayload());
    probe.handle(probePayload());
    probe.drain();

    CHECK(probe.received.size() == 1);
}

TEST_CASE("firmware_probe without a usable run_id raises no event")
{
    ProbeNet probe;

    auto missing = probePayload();
    missing.erase("run_id");
    auto notString = probePayload();
    notString["run_id"] = 42;
    auto traversal = probePayload("/internal/api/diagnostics/hardware-probe-result/../../x/");
    traversal["run_id"] = "../../x";

    probe.handle(missing);
    probe.handle(notString);
    probe.handle(traversal);
    probe.drain();

    CHECK(probe.received.empty());
}

TEST_CASE("firmware_probe queues exactly one SDK section post for a valid request")
{
    ProbeNet probe;

    probe.handle(probePayload());
    CHECK(NetTestAccess::hardwareProbeSdkPosts(probe.net) == 1);

    // A duplicate run_id is refused before anything is posted
    probe.handle(probePayload());
    CHECK(NetTestAccess::hardwareProbeSdkPosts(probe.net) == 1);
}

TEST_CASE("firmware_probe queues no SDK section post for a refused request")
{
    auto invalidRunId = probePayload("/internal/api/diagnostics/hardware-probe-result/../../x/");
    invalidRunId["run_id"] = "../../x";
    const auto wrongReplyTo =
            probePayload("/internal/api/diagnostics/hardware-probe-result/other-run/");

    for (const auto &payload : {invalidRunId, wrongReplyTo}) {
        CAPTURE(payload.dump());
        ProbeNet probe;
        probe.handle(payload);
        CHECK(NetTestAccess::hardwareProbeSdkPosts(probe.net) == 0);
    }
}

TEST_CASE("submitHardwareProbeResult POSTs the body verbatim to the run's result path")
{
    Net net {DeviceInfo {}, {}};
    const std::string body = R"({"end_reason":"completed","devices":[]})";
    ScriptedTransport transport {{makeResponse(409, R"({"detail":"conflict"})")}};

    Error seenError {Error::Success};
    int seenStatus = -1;
    HttpStatusCallback callback = [&](Error error, int httpStatus, const std::string &) {
        seenError = error;
        seenStatus = httpStatus;
    };

    NetTestAccess::hardwareProbeResult(net, PROBE_RUN_ID, body, std::move(callback),
                                       std::ref(transport))();

    CHECK(transport.calls == 1);
    CHECK(transport.lastUrl == net.hostname() + PROBE_REPLY_TO);
    CHECK(transport.lastBody == body);
    CHECK(seenStatus == 409);
    CHECK(seenError == Error::ApiError);
}

TEST_CASE("submitHardwareProbeResult rejects invalid input without sending")
{
    Net net {DeviceInfo {}, {}};

    const std::vector<std::pair<std::string, std::string>> cases {
            {PROBE_RUN_ID, "not json"}, {PROBE_RUN_ID, "[1]"}, {"", "{}"}, {"a/b", "{}"}};
    for (const auto &[runId, resultJson] : cases) {
        CAPTURE(runId, resultJson);
        int calls = 0;
        Error seenError {Error::Success};
        int seenStatus = -1;
        net.submitHardwareProbeResult(runId, resultJson,
                                      [&](Error error, int httpStatus, const std::string &) {
                                          ++calls;
                                          seenError = error;
                                          seenStatus = httpStatus;
                                      });
        // Answered synchronously: nothing was queued for the worker.
        CHECK(calls == 1);
        CHECK(seenError == Error::Unknown);
        CHECK(seenStatus == 0);
    }
}

namespace {

/// Minimal NFC probe: identity read OK, tag mode, no tap pending.
class StubNfcProbe : public nfc::NfcProbeDevice
{
public:
    int reads {0};

    bool getInformations(nfc::ProbeInformation &info) override
    {
        ++reads;
        info.id = "NFC";
        info.name = "NFC";
        info.versionMajor = 1;
        info.bootReason = 2;
        return true;
    }
    bool getNfcInformations(nfc::NfcInformation &info) override
    {
        info.flags = nfc::NfcFlagTag;
        return true;
    }
    bool setUri(const std::string &) override { return true; }
    bool setLedsBrightness(uint8_t) override { return true; }
    bool setType(nfc::NfcType) override { return true; }
};

constexpr auto PROBE_SDK_RESULT_PATH =
        "/internal/api/diagnostics/hardware-probe-result/3f2b8c1e-8d4a-4a57-9a3e-2c6f1d0b7e55/sdk/";

} // namespace

TEST_CASE("The SDK's hardware probe section POSTs the NFC probe read to the run's sdk path")
{
    Net net {DeviceInfo {}, {}};
    NetTestAccess::setProbesManager(
            net, std::make_shared<nfc::ProbesManager>(std::make_shared<StubNfcProbe>()));
    ScriptedTransport transport {{makeResponse(201)}};

    NetTestAccess::hardwareProbeSdkResult(net, PROBE_RUN_ID, std::ref(transport))();

    REQUIRE(transport.calls == 1);
    CHECK(transport.lastUrl == net.hostname() + PROBE_SDK_RESULT_PATH);
    const auto body = nlohmann::json::parse(transport.lastBody);
    CHECK(body["sdk_version"] == SCORBIT_SDK_VERSION);
    CHECK(body["nfc_status"] == "ok");
    CHECK(body["nfc_mode"] == "tag");
    CHECK(body["nfc_extended"]["boot_reason"] == "bootloader_restart");
}

TEST_CASE("A forced hardware probe rescans and reports a probe found after start")
{
    // No NFC probe at startup: the cached capability is false
    auto late = std::make_shared<StubNfcProbe>();
    int scans = 0;
    auto scanner = [&](nfc::probe_t, const nfc::ProbeDisplayCallback &) {
        ++scans;
        return std::shared_ptr<nfc::NfcProbeDevice> {late};
    };

    SECTION("forced: rescans once and reports ok")
    {
        Net net {DeviceInfo {}, {}};
        NetTestAccess::setProbesManager(net,
                                        std::make_shared<nfc::ProbesManager>(nullptr, scanner));
        ScriptedTransport transport {{makeResponse(0), makeResponse(201)}};

        NetTestAccess::hardwareProbeSdkResult(net, PROBE_RUN_ID, std::ref(transport), true)();

        CHECK(transport.calls == 2);
        CHECK(scans == 1); // once per request, not per retry
        CHECK(nlohmann::json::parse(transport.lastBody)["nfc_status"] == "ok");
    }

    SECTION("not forced: no rescan")
    {
        Net net {DeviceInfo {}, {}};
        NetTestAccess::setProbesManager(net,
                                        std::make_shared<nfc::ProbesManager>(nullptr, scanner));
        ScriptedTransport transport {{makeResponse(201)}};

        NetTestAccess::hardwareProbeSdkResult(net, PROBE_RUN_ID, std::ref(transport), false)();

        CHECK(scans == 0);
        CHECK(nlohmann::json::parse(transport.lastBody)["nfc_status"] == "not_present");
    }
}

TEST_CASE("The SDK's hardware probe section reads the probe once across retries")
{
    Net net {DeviceInfo {}, {}};
    auto probe = std::make_shared<StubNfcProbe>();
    NetTestAccess::setProbesManager(net, std::make_shared<nfc::ProbesManager>(probe));
    // A transport failure is retried; the retry must resend the same body, not re-read the pad
    ScriptedTransport transport {{makeResponse(0), makeResponse(201)}};

    NetTestAccess::hardwareProbeSdkResult(net, PROBE_RUN_ID, std::ref(transport))();

    CHECK(transport.calls == 2);
    CHECK(probe->reads == 1);
    CHECK(nlohmann::json::parse(transport.lastBody)["nfc_status"] == "ok");
}

TEST_CASE("The SDK's hardware probe section reports not_present without an NFC probe")
{
    for (const bool withManager : {false, true}) {
        CAPTURE(withManager);
        Net net {DeviceInfo {}, {}};
        if (withManager) {
            NetTestAccess::setProbesManager(net, std::make_shared<nfc::ProbesManager>(nullptr));
        }
        ScriptedTransport transport {{makeResponse(201)}};

        NetTestAccess::hardwareProbeSdkResult(net, PROBE_RUN_ID, std::ref(transport))();

        REQUIRE(transport.calls == 1);
        CHECK(transport.lastUrl == net.hostname() + PROBE_SDK_RESULT_PATH);
        CHECK(nlohmann::json::parse(transport.lastBody)
              == nlohmann::json {{"sdk_version", SCORBIT_SDK_VERSION},
                                 {"nfc_status", "not_present"},
                                 {"nfc_extended",
                                  {{"last_seen_tap_id", 0}, {"last_seen_tap_unix_ms", 0}}}});
    }
}

// ------------------------------ File download task --------------------------------------------

namespace {

std::string readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Replays one scripted attempt per call: writes `body` to the stream, then returns `response`.
struct ScriptedDownload {
    struct Attempt {
        std::string body;
        cpr::Response response;
        bool breakStream {false};
    };

    std::vector<Attempt> attempts;
    int calls {0};
    std::vector<std::string> urls;    ///< the URL of each call, redirects included
    std::vector<cpr::Header> headers; ///< the headers of each call

    cpr::Response operator()(std::ofstream &file, const cpr::Url &url, const cpr::Header &sent)
    {
        urls.push_back(url.str());
        headers.push_back(sent);
        const auto &a = attempts[std::min<size_t>(calls++, attempts.size() - 1)];
        file << a.body;
        if (a.breakStream) {
            file.setstate(std::ios::badbit);
        }
        return a.response;
    }
};

cpr::Response downloadResponse(int status, cpr::ErrorCode code = cpr::ErrorCode::OK)
{
    cpr::Response r = makeResponse(status);
    r.error.code = code;
    return r;
}

cpr::Response redirectResponse(int status, std::string location)
{
    cpr::Response r = downloadResponse(status);
    r.header["Location"] = std::move(location);
    return r;
}

DownloadOptions redirectOptions(RedirectPolicy policy, std::vector<std::string> hosts = {})
{
    DownloadOptions options;
    options.redirect = policy;
    options.redirectHosts = std::move(hosts);
    options.given = true;
    return options;
}

struct DownloadFixture {
    std::filesystem::path dir {
            std::filesystem::temp_directory_path()
            / boost::filesystem::unique_path("sdk_dl_test_%%%%-%%%%-%%%%").string()};
    std::filesystem::path dest {dir / "out.bin"};
    DeviceInfo info;
    Net net {std::move(info), {}};
    Error seenError {Error::Success};

    DownloadFixture() { std::filesystem::create_directories(dir); }
    ~DownloadFixture()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    void seedDestination(const std::string &content) { std::ofstream(dest) << content; }

    void run(ScriptedDownload &transport, DownloadOptions options = {}, HttpHeaders headers = {})
    {
        NetTestAccess::downloadFile(
                net,
                [this](Error e, const std::string &reply) {
                    seenError = e;
                    seenReply = reply;
                },
                "http://x.invalid/f", dest.string(), std::ref(transport), options,
                std::move(headers))();
    }

    std::string seenReply;

    size_t entries() const
    {
        return std::distance(std::filesystem::directory_iterator(dir),
                             std::filesystem::directory_iterator());
    }
};

} // namespace

TEST_CASE("A download past its byte limit fails, is not retried, and leaves no file")
{
    DownloadFixture f;
    f.seedDestination("ORIGINAL");
    DownloadOptions options;
    options.maxBytes = 8;

    ScriptedDownload transport {{{"123456789", downloadResponse(200)}}};
    f.run(transport, options);

    CHECK(transport.calls == 1);
    CHECK(f.seenError == Error::LimitExceeded);
    CHECK(f.seenReply.find("refused: body exceeds the 8 byte limit") != std::string::npos);
    CHECK(readFile(f.dest) == "ORIGINAL");
    CHECK(f.entries() == 1); // no temp file left behind
}

TEST_CASE("An error page past the byte limit is still the error it is")
{
    DownloadFixture f;
    DownloadOptions options;
    options.maxBytes = 8;
    ScriptedDownload transport {{{"<html>not found, at length</html>", downloadResponse(404)}}};

    f.run(transport, options);

    CHECK(f.seenError == Error::ApiError);
    CHECK(f.seenReply.find("refused") == std::string::npos);
    CHECK(f.entries() == 0);
}

TEST_CASE("A download within its byte limit succeeds")
{
    DownloadFixture f;
    DownloadOptions options;
    options.maxBytes = 9;
    ScriptedDownload transport {{{"123456789", downloadResponse(200)}}};

    f.run(transport, options);

    CHECK(f.seenError == Error::Success);
    CHECK(readFile(f.dest) == "123456789");
}

TEST_CASE("The download sink writes up to its limit and never past it")
{
    std::ostringstream out;
    DownloadSink sink {out, 8};

    CHECK(sink.write("1234"));
    CHECK(sink.write("5678")); // exactly at the limit
    CHECK_FALSE(sink.limitHit());
    CHECK_FALSE(sink.write("9")); // one byte over: refused, which aborts the transfer
    CHECK(sink.limitHit());
    CHECK(out.str() == "12345678"); // nothing past the limit was written
    CHECK(sink.written() == 8);

    SECTION("a failed stream stops the transfer without counting as the limit")
    {
        std::ostringstream broken;
        broken.setstate(std::ios::badbit);
        DownloadSink failing {broken, 100};
        CHECK_FALSE(failing.write("12"));
        CHECK_FALSE(failing.limitHit());
    }
}

TEST_CASE("A download curl redirected itself reports where it ended")
{
    DownloadFixture f;
    DownloadOptions options = redirectOptions(RedirectPolicy::Follow); // explicit "follow"
    SECTION("with a byte limit")
    {
        options.maxBytes = 100;
    }
    SECTION("with nothing but the policy")
    {
    }
    cpr::Response ended = downloadResponse(200);
    ended.url = cpr::Url {"https://cdn.example.com/moved/f"};
    ScriptedDownload transport {{{"BODY", ended}}};

    f.run(transport, options);

    CHECK(f.seenError == Error::Success);
    CHECK(f.seenReply.find("final url: https://cdn.example.com/moved/f") != std::string::npos);
}

TEST_CASE("A download with redirects refused stops at the redirect, naming its target")
{
    DownloadFixture f;
    f.seedDestination("ORIGINAL");
    ScriptedDownload transport {{{"moved", redirectResponse(302, "https://cdn.example.com/f")},
                                 {"BODY", downloadResponse(200)}}};

    f.run(transport, redirectOptions(RedirectPolicy::None));

    CHECK(transport.calls == 1); // never followed, and not retried
    CHECK(f.seenError == Error::LimitExceeded);
    CHECK(f.seenReply.find("refused: redirect to cdn.example.com") != std::string::npos);
    CHECK(readFile(f.dest) == "ORIGINAL");
    CHECK(f.entries() == 1);
}

TEST_CASE("A download limited to listed hosts follows only those")
{
    DownloadFixture f;
    const auto hosts = redirectOptions(RedirectPolicy::Hosts, {"cdn.example.com"});

    SECTION("a listed host is followed, and the reply names where it ended")
    {
        ScriptedDownload transport {{{"moved", redirectResponse(302, "http://CDN.example.com/f")},
                                     {"BODY", downloadResponse(200)}}};
        f.run(transport, hosts);
        CHECK(f.seenError == Error::Success);
        CHECK(readFile(f.dest) == "BODY"); // the redirect's own body is not kept
        REQUIRE(transport.urls.size() == 2);
        CHECK(transport.urls[1] == "http://CDN.example.com/f");
        CHECK(f.seenReply.find("final url: http://CDN.example.com/f") != std::string::npos);
    }
    SECTION("a relative Location resolves against the URL it came from")
    {
        ScriptedDownload transport {{{"", redirectResponse(301, "/elsewhere/g")},
                                     {"", redirectResponse(307, "https://cdn.example.com/h")},
                                     {"BODY", downloadResponse(200)}}};
        DownloadOptions both =
                redirectOptions(RedirectPolicy::Hosts, {"x.invalid", "cdn.example.com"});
        f.run(transport, both);
        CHECK(f.seenError == Error::Success);
        REQUIRE(transport.urls.size() == 3);
        CHECK(transport.urls[1] == "http://x.invalid/elsewhere/g");
    }
    SECTION("any 3xx with a Location is a redirect, 300 included")
    {
        ScriptedDownload transport {{{"", redirectResponse(300, "https://cdn.example.com/f")},
                                     {"BODY", downloadResponse(200)}}};
        f.run(transport, hosts);
        CHECK(f.seenError == Error::Success);
        CHECK(transport.calls == 2);
    }
    SECTION("an unlisted host is refused before any of its body is written")
    {
        ScriptedDownload transport {{{"", redirectResponse(302, "https://evil.example.net/f")},
                                     {"EVIL", downloadResponse(200)}}};
        f.run(transport, hosts);
        CHECK(transport.calls == 1);
        CHECK(f.seenError == Error::LimitExceeded);
        CHECK(f.seenReply.find("redirect to evil.example.net") != std::string::npos);
        CHECK(f.entries() == 0);
    }
    SECTION("a listed host is refused when the redirect drops https for http")
    {
        ScriptedDownload transport {{{"", redirectResponse(302, "https://cdn.example.com/f")},
                                     {"", redirectResponse(302, "http://cdn.example.com/g")},
                                     {"BODY", downloadResponse(200)}}};
        f.run(transport, hosts);
        CHECK(transport.calls == 2);
        CHECK(f.seenError == Error::LimitExceeded);
    }
    SECTION("an endless chain stops after a few hops")
    {
        ScriptedDownload transport {{{"", redirectResponse(302, "https://cdn.example.com/again")}}};
        f.run(transport, hosts);
        CHECK(transport.calls == 6); // the request and 5 redirects followed
        CHECK(f.seenError == Error::LimitExceeded);
        CHECK(f.seenReply.find("refused: more than 5 redirects") != std::string::npos);
    }
    SECTION("the caller's credentials are not sent to another host")
    {
        ScriptedDownload transport {{{"", redirectResponse(302, "https://cdn.example.com/f")},
                                     {"BODY", downloadResponse(200)}}};
        f.run(transport, hosts,
              {{"Authorization", "Bearer secret"}, {"COOKIE", "c=1"}, {"Accept", "*/*"}});
        REQUIRE(transport.headers.size() == 2);
        CHECK(transport.headers[0].count("Authorization") == 1);
        CHECK(transport.headers[1].count("Authorization") == 0);
        CHECK(transport.headers[1].count("Cookie") == 0);
        CHECK(transport.headers[1].at("Accept") == "*/*");
    }
    SECTION("a requested URL curl accepts but a strict parser would not still has its redirects "
            "checked")
    {
        ScriptedDownload transport {{{"", redirectResponse(302, "https://cdn.example.com/f")},
                                     {"", redirectResponse(302, "/g")},
                                     {"BODY", downloadResponse(200)}}};
        DownloadFixture raw;
        NetTestAccess::downloadFile(
                raw.net,
                [&raw](Error e, const std::string &reply) {
                    raw.seenError = e;
                    raw.seenReply = reply;
                },
                "http://x.invalid/a|b^c[1]", raw.dest.string(), std::ref(transport), hosts)();
        INFO(raw.seenReply);
        CHECK(raw.seenError == Error::Success);
        REQUIRE(transport.urls.size() == 3);
        CHECK(transport.urls[2] == "https://cdn.example.com/g");
        CHECK(redirectAllowed(hosts, "http://x.invalid/a|b", "https://cdn.example.com/f"));
        CHECK(hostForReply("http://x.invalid/a|b") == "x.invalid");
    }
    SECTION("the caller's credentials are not sent to another port of the same host")
    {
        ScriptedDownload transport {{{"", redirectResponse(302, "http://x.invalid:80/same")},
                                     {"", redirectResponse(302, "http://x.invalid:8080/f")},
                                     {"BODY", downloadResponse(200)}}};
        f.run(transport, redirectOptions(RedirectPolicy::Hosts, {"x.invalid"}),
              {{"Authorization", "Bearer secret"}, {"Cookie", "c=1"}});
        REQUIRE(transport.headers.size() == 3);
        CHECK(transport.headers[1].count("Cookie") == 1); // port 80 is http's own
        CHECK(transport.headers[2].count("Authorization") == 0);
        CHECK(transport.headers[2].count("Cookie") == 0);
    }
    SECTION("device auth follows the API host whatever the case of its name")
    {
        f.net.setHostname("http://x.invalid");
        ScriptedDownload transport {{{"", redirectResponse(302, "http://X.INVALID/g")},
                                     {"", redirectResponse(302, "https://cdn.example.com/h")},
                                     {"BODY", downloadResponse(200)}}};
        f.run(transport, redirectOptions(RedirectPolicy::Hosts, {"x.invalid", "cdn.example.com"}));
        CHECK(f.seenError == Error::Success);
        REQUIRE(transport.headers.size() == 3);
        CHECK(transport.headers[0].count("Authorization") == 1);
        CHECK(transport.headers[1].count("Authorization") == 1);
        CHECK(transport.headers[2].count("Authorization") == 0);
    }
}

namespace {

std::string httpResponse(const std::string &status, const std::string &headers,
                         const std::string &body)
{
    return "HTTP/1.1 " + status + "\r\n" + headers + "Content-Length: "
         + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
}

/// Answers GETs on a local port with canned responses, so curl itself handles the transfer.
class LocalHttpServer
{
public:
    explicit LocalHttpServer(std::map<std::string, std::string> routes)
        : m_routes {std::move(routes)}
    {
        accept();
        m_thread = std::thread([this] { m_io.run(); });
    }
    ~LocalHttpServer()
    {
        m_io.stop();
        m_thread.join();
    }

    std::string url(const std::string &path) const
    {
        return "http://127.0.0.1:" + std::to_string(m_acceptor.local_endpoint().port()) + path;
    }

private:
    void accept()
    {
        m_acceptor.async_accept([this](boost::system::error_code ec, tcp::socket s) {
            if (ec) {
                return;
            }
            auto socket = std::make_shared<tcp::socket>(std::move(s));
            auto request = std::make_shared<boost::asio::streambuf>();
            boost::asio::async_read_until(
                    *socket, *request, "\r\n\r\n",
                    [this, socket, request](boost::system::error_code ec, size_t) {
                        if (ec) {
                            return;
                        }
                        std::istream in {request.get()};
                        std::string method, path;
                        in >> method >> path;
                        const auto it = m_routes.find(path);
                        auto reply = std::make_shared<std::string>(
                                it != m_routes.end() ? it->second
                                                     : httpResponse("404 Not Found", "", ""));
                        boost::asio::async_write(
                                *socket, boost::asio::buffer(*reply),
                                [socket, reply](boost::system::error_code, size_t) {});
                    });
            accept();
        });
    }

    std::map<std::string, std::string> m_routes;
    boost::asio::io_context m_io;
    tcp::acceptor m_acceptor {m_io, {boost::asio::ip::make_address("127.0.0.1"), 0}};
    std::thread m_thread;
};

/// @p data as a gzip member holding one stored deflate block, so it encodes larger than it is
std::string gzipStored(const std::string &data)
{
    std::string out {"\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\xff", 10};
    const auto len = static_cast<uint16_t>(data.size());
    out += '\x01'; // the final block, stored
    for (const uint16_t v : {len, static_cast<uint16_t>(~len)}) {
        out += char(v & 0xFF);
        out += char(v >> 8);
    }
    out += data;
    uint32_t crc = 0xFFFFFFFF; // CRC-32 as gzip uses it, bit by bit
    for (const unsigned char c : data) {
        crc ^= c;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
        }
    }
    for (const uint32_t v : {~crc, static_cast<uint32_t>(data.size())}) {
        for (int i = 0; i < 4; ++i) {
            out += char((v >> (8 * i)) & 0xFF);
        }
    }
    return out;
}

/// Downloads @p url over the network into @p f's destination.
void realDownload(DownloadFixture &f, const std::string &url, const DownloadOptions &options)
{
    NetTestAccess::downloadFile(
            f.net,
            [&f](Error e, const std::string &reply) {
                f.seenError = e;
                f.seenReply = reply;
            },
            url, f.dest.string(), {}, options)();
}

} // namespace

TEST_CASE("A redirect announcing a body past the limit still leads to a small file")
{
    const std::string big(100000, 'x');
    LocalHttpServer server {{
            {"/redirect", httpResponse("302 Found", "Location: /small\r\n", big)},
            {"/small", httpResponse("200 OK", "", "SMALL")},
            {"/big", httpResponse("200 OK", "", big)},
    }};
    DownloadFixture f;
    DownloadOptions options;
    SECTION("when curl follows the redirect")
    {
        options = redirectOptions(RedirectPolicy::Follow);
    }
    SECTION("when it is followed by hand")
    {
        options = redirectOptions(RedirectPolicy::Hosts, {"127.0.0.1"});
    }
    options.maxBytes = 100;

    realDownload(f, server.url("/redirect"), options);
    INFO(f.seenReply);
    CHECK(f.seenError == Error::Success);
    CHECK(readFile(f.dest) == "SMALL");

    realDownload(f, server.url("/big"), options); // a final body past the limit is still refused
    CHECK(f.seenError == Error::LimitExceeded);
    CHECK(readFile(f.dest) == "SMALL");
}

TEST_CASE("A compressed body is limited by its decoded size, not the size it was sent at")
{
    // The SDK never asks for compression, so only an unasked gzip body reaches here, and curl can
    // decode it only when built with zlib -- the Linux release builds are not (SB-6104).
    if (!(curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_LIBZ)) {
        SKIP("libcurl is built without zlib");
    }
    const std::string limit(100, 'z');
    LocalHttpServer server {{
            {"/at", httpResponse("200 OK", "Content-Encoding: gzip\r\n", gzipStored(limit))},
            {"/past",
             httpResponse("200 OK", "Content-Encoding: gzip\r\n", gzipStored(limit + "z"))},
    }};
    REQUIRE(gzipStored(limit).size() > limit.size()); // sent at more than the limit
    DownloadFixture f;
    DownloadOptions options;
    SECTION("when curl follows redirects")
    {
        options = redirectOptions(RedirectPolicy::Follow);
    }
    SECTION("when they are followed by hand")
    {
        options = redirectOptions(RedirectPolicy::Hosts, {"127.0.0.1"});
    }
    options.maxBytes = limit.size();

    realDownload(f, server.url("/at"), options);
    INFO(f.seenReply);
    CHECK(f.seenError == Error::Success);
    CHECK(readFile(f.dest) == limit);

    realDownload(f, server.url("/past"), options);
    CHECK(f.seenError == Error::LimitExceeded);
    CHECK(readFile(f.dest) == limit);
}

TEST_CASE("A redirect Location with raw spaces still resolves")
{
    CHECK(resolveRedirect("https://a.example.com/x", "https://cdn.example.com/a file.bin")
          == "https://cdn.example.com/a%20file.bin");
    CHECK(resolveRedirect("https://a.example.com/x/y", "../b c") == "https://a.example.com/b%20c");
}

TEST_CASE("An origin is the scheme, the host in any case, and the effective port")
{
    CHECK(isSameOrigin("https://a.example.com/x", "HTTPS://A.Example.com:443/y"));
    CHECK(isSameOrigin("http://a.example.com:80/x", "http://a.example.com/y"));
    CHECK_FALSE(isSameOrigin("https://a.example.com/x", "http://a.example.com/x"));
    CHECK_FALSE(isSameOrigin("https://a.example.com/x", "https://a.example.com:8443/x"));
    CHECK_FALSE(isSameOrigin("https://a.example.com/x", "https://b.example.com/x"));
    CHECK_FALSE(isSameOrigin("https://a.example.com/x", "not a url"));
    CHECK(isSameOrigin("http://a@b@x.invalid/f",
                       "http://a@b@x.invalid/f")); // unparsed, still itself
}

TEST_CASE("A download keeps the caller's credentials on the URL it was asked for, parsed or not")
{
    DownloadFixture f;
    ScriptedDownload transport {{{"BODY", downloadResponse(200)}}};
    NetTestAccess::downloadFile(f.net, [&f](Error e, const std::string &) { f.seenError = e; },
                                "http://a@b@x.invalid/f", f.dest.string(), std::ref(transport), {},
                                {{"Authorization", "Bearer secret"}, {"Cookie", "c=1"}})();

    CHECK(f.seenError == Error::Success);
    REQUIRE(transport.headers.size() == 1);
    CHECK(transport.headers[0].count("Authorization") == 1);
    CHECK(transport.headers[0].count("Cookie") == 1);
}

TEST_CASE("A redirect Location with a stray percent or brackets still resolves")
{
    const std::string from = "https://a.example.com/x";
    CHECK(resolveRedirect(from, "/50%off") == "https://a.example.com/50%25off");
    CHECK(resolveRedirect(from, "/end%") == "https://a.example.com/end%25");
    CHECK(resolveRedirect(from, "/end%4") == "https://a.example.com/end%254");
    CHECK(resolveRedirect(from, "/ok%2Fx") == "https://a.example.com/ok%2Fx"); // kept as sent
    CHECK(resolveRedirect(from, "/a[1].bin?v=[2]#[3]")
          == "https://a.example.com/a%5B1%5D.bin?v=%5B2%5D#%5B3%5D");
    // An IPv6 host keeps its brackets; the rest of the URL does not
    CHECK(resolveRedirect(from, "http://[::1]:8080/p[0]") == "http://[::1]:8080/p%5B0%5D");
    CHECK(resolveRedirect(from, "//[::1]/q?x=[1]") == "https://[::1]/q?x=%5B1%5D");
}

TEST_CASE("Download options")
{
    {
        const auto hosts = parseDownloadOptions(
                R"({"redirect":"hosts","redirect_hosts":["CDN.example.com"]})");
        REQUIRE(hosts);
        CHECK(hosts->redirect == RedirectPolicy::Hosts);
        CHECK(hosts->redirectHosts == std::vector<std::string> {"cdn.example.com"});
    }
    CHECK(parseDownloadOptions(R"({"redirect":"none"})")->redirect == RedirectPolicy::None);
    CHECK(parseDownloadOptions(R"({"redirect":"follow"})")->redirect == RedirectPolicy::Follow);
    CHECK_FALSE(parseDownloadOptions(R"({"redirect":"hosts"})")); // no list
    CHECK_FALSE(parseDownloadOptions(R"({"redirect":"hosts","redirect_hosts":[]})"));
    CHECK_FALSE(parseDownloadOptions(R"({"redirect_hosts":["cdn.example.com"]})")); // no policy
    CHECK_FALSE(parseDownloadOptions(R"({"redirect":"none","redirect_hosts":["a.b"]})"));
    CHECK_FALSE(parseDownloadOptions(R"({"redirect":"sometimes"})"));
    CHECK_FALSE(parseDownloadOptions(R"({"redirect":"hosts","redirect_hosts":[""]})"));

    REQUIRE(parseDownloadOptions(""));
    CHECK_FALSE(parseDownloadOptions("")->maxBytes);
    REQUIRE(parseDownloadOptions(R"({"max_bytes":4096})"));
    CHECK(parseDownloadOptions(R"({"max_bytes":4096})")->maxBytes == 4096u);
    CHECK(parseDownloadOptions("{}"));
    CHECK_FALSE(parseDownloadOptions(R"({"max_bytes":-1})"));
    CHECK_FALSE(parseDownloadOptions(R"({"max_bytes":"4096"})"));
    CHECK_FALSE(parseDownloadOptions(R"({"timeout":5})")); // not known: refused, not ignored
    CHECK_FALSE(parseDownloadOptions("[1]"));
    CHECK_FALSE(parseDownloadOptions("max_bytes=1"));
}

TEST_CASE("A download retried after a truncated 200 yields only the second body")
{
    DownloadFixture f;
    f.seedDestination("ORIGINAL");
    ScriptedDownload transport {{{"PARTIAL", downloadResponse(200, cpr::ErrorCode::PARTIAL_FILE)},
                                 {"FULL-BODY", downloadResponse(200)}}};

    f.run(transport);

    CHECK(transport.calls == 2);
    CHECK(f.seenError == Error::Success);
    CHECK(readFile(f.dest) == "FULL-BODY");
    CHECK(f.entries() == 1); // no temp file left behind
}

TEST_CASE("A download retried after a transport failure yields only the second body")
{
    DownloadFixture f;
    ScriptedDownload transport {
            {{"PARTIAL", downloadResponse(0, cpr::ErrorCode::OPERATION_TIMEDOUT)},
             {"FULL-BODY", downloadResponse(200)}}};

    f.run(transport);

    CHECK(f.seenError == Error::Success);
    CHECK(readFile(f.dest) == "FULL-BODY");
}

TEST_CASE("A download that always fails leaves no file and keeps an existing destination")
{
    DownloadFixture f;
    f.seedDestination("ORIGINAL");
    ScriptedDownload transport {
            {{"JUNK", downloadResponse(0, cpr::ErrorCode::OPERATION_TIMEDOUT)}}};

    f.run(transport);

    CHECK(transport.calls > 1);
    CHECK(f.seenError == Error::ApiError);
    CHECK(readFile(f.dest) == "ORIGINAL");
    CHECK(f.entries() == 1);
}

TEST_CASE("A failed download with no existing destination leaves the directory empty")
{
    DownloadFixture f;
    ScriptedDownload transport {
            {{"JUNK", downloadResponse(0, cpr::ErrorCode::OPERATION_TIMEDOUT)}}};

    f.run(transport);

    CHECK(f.seenError == Error::ApiError);
    CHECK(f.entries() == 0);
}

TEST_CASE("A write failure is a FileError even when the status is 200")
{
    DownloadFixture f;
    f.seedDestination("ORIGINAL");
    ScriptedDownload transport {{{"X", downloadResponse(200), true}}};

    f.run(transport);

    CHECK(transport.calls == 1); // not retried
    CHECK(f.seenError == Error::FileError);
    CHECK(readFile(f.dest) == "ORIGINAL");
    CHECK(f.entries() == 1);
}

TEST_CASE("A 4xx download is not retried and leaves the destination unchanged")
{
    DownloadFixture f;
    f.seedDestination("ORIGINAL");
    ScriptedDownload transport {{{"error page", downloadResponse(404)}}};

    f.run(transport);

    CHECK(transport.calls == 1);
    CHECK(f.seenError == Error::ApiError);
    CHECK(readFile(f.dest) == "ORIGINAL");
    CHECK(f.entries() == 1);
}
