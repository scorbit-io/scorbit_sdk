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
#include <scorbit_sdk/scorbit_sdk.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/trompeloeil.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <vector>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

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
    CHECK(net.cfHostname() == "wss://sws.scorbit.io:443");

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

    cpr::Response operator()(const cpr::Url &, const cpr::Body &, const cpr::Header &,
                             const cpr::Timeout &, bool)
    {
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
