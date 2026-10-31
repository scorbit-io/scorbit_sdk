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

// SB-5165: the authentication task must never strand a device after a failure it can recover from.
//
// Every Net here is built from an empty DeviceInfo, so its worker never starts: nothing posted
// runs, and the authentication task is run inline through NetTestAccess. All three network calls
// the task makes go through a scripted transport, and retries are captured instead of timed.

#include "net.h"
#include "net_util.h"
#include "utils/decrypt.h"
#include <utils/bytearray.h>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace scorbit;
using namespace scorbit::detail;
using namespace std::chrono;
using namespace std::chrono_literals;

namespace {

std::string httpDate(system_clock::time_point at)
{
    const auto t = system_clock::to_time_t(at);
    std::ostringstream out;
    out << std::put_time(std::gmtime(&t), "%a, %d %b %Y %H:%M:%S GMT");
    return out.str();
}

std::string base64Url(const std::string &in)
{
    static constexpr char table[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    unsigned value = 0;
    int bits = -6;
    for (const unsigned char c : in) {
        value = (value << 8) + c;
        bits += 8;
        while (bits >= 0) {
            out.push_back(table[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) {
        out.push_back(table[((value << 8) >> (bits + 8)) & 0x3F]);
    }
    return out;
}

/// An unsigned JWT expiring @p expiresIn from now; only its expiry is ever read.
std::string jwt(seconds expiresIn)
{
    const nlohmann::json header {{"alg", "HS256"}, {"typ", "JWT"}};
    const nlohmann::json payload {
            {"exp",
             duration_cast<seconds>((system_clock::now() + expiresIn).time_since_epoch()).count()}};
    return base64Url(header.dump()) + "." + base64Url(payload.dump()) + ".sig";
}

cpr::Response response(int status, std::string body = {}, cpr::Header header = {})
{
    cpr::Response r;
    r.status_code = status;
    r.text = std::move(body);
    r.header = std::move(header);
    if (r.header.find("Date") == r.header.end()) {
        r.header["Date"] = httpDate(system_clock::now());
    }
    return r;
}

cpr::Response tokenReply(const std::string &token)
{
    return response(200, nlohmann::json {{"token", token}}.dump());
}

/// Records the authentication requests a retry makes instead of posting them to the worker.
struct CountingNet : Net {
    CountingNet(std::vector<std::unique_ptr<IKeyResolver>> resolvers, DeviceInfo info,
                std::shared_ptr<const HardwareKeyMarker> marker = {})
        : Net(std::move(info), std::move(resolvers), std::move(marker))
    {
    }

    void authenticate() override { ++authenticateCalls; }

    int authenticateCalls = 0;
};

struct Retry {
    AuthRetryKind kind;
    seconds delay;
    detail::task_t fire;
};

struct AuthHarness {
    explicit AuthHarness(std::vector<std::unique_ptr<IKeyResolver>> resolvers = {},
                         bool withSigner = true, DeviceInfo info = {},
                         std::shared_ptr<const HardwareKeyMarker> marker = {})
        : net(std::move(resolvers), std::move(info), std::move(marker))
    {
        NetTestAccess::setAuthTransport(
                net,
                {
                        .noop =
                                [this](const cpr::Url &) {
                                    return noopReply ? noopReply() : response(200);
                                },
                        .token =
                                [this](const cpr::Url &, const cpr::Body &body,
                                       const cpr::Header &) {
                                    ++tokenCalls;
                                    lastTokenBody = body.str();
                                    if (beforeToken) {
                                        beforeToken();
                                    }
                                    if (script.empty()) {
                                        FAIL("token endpoint called with nothing scripted");
                                    }
                                    auto r = script.front();
                                    if (script.size() > 1) {
                                        script.pop_front();
                                    }
                                    return r;
                                },
                        .centrifugoToken = [](const std::string &,
                                              const std::string &) { return std::string {}; },
                        .setSystemTime =
                                [this](std::int64_t epoch) {
                                    clockSets.push_back(epoch);
                                    return true;
                                },
                },
                {
                        .start =
                                [this](AuthRetryKind kind, seconds delay, detail::task_t fire) {
                                    retries.push_back({kind, delay, std::move(fire)});
                                },
                        .stop =
                                [this] {
                                    ++timerStops;
                                    retries.clear();
                                },
                        .random = [this] { return jitter; },
                });
        if (withSigner) {
            NetTestAccess::setSigner(net, [this](const Digest &) { return signature; });
        }
    }

    /// Script the token endpoint; the last reply repeats.
    void replies(std::vector<cpr::Response> next) { script.assign(next.begin(), next.end()); }

    void run() { NetTestAccess::runAuthentication(net); }

    Retry takeRetry()
    {
        REQUIRE(retries.size() == 1);
        auto retry = std::move(retries.front());
        retries.clear();
        return retry;
    }

    AuthStatus status() const { return NetTestAccess::status(net); }

    CountingNet net;
    std::deque<cpr::Response> script;
    int tokenCalls = 0;
    std::vector<Retry> retries;
    int timerStops = 0;
    double jitter = 0.0; // the retry jitter's random draw; 0 = no jitter
    std::string lastTokenBody;
    /// Runs inside the token POST, e.g. to make the transport throw.
    std::function<void()> beforeToken;
    /// Replaces the server-time request's reply; unset, a 200 dated now.
    std::function<cpr::Response()> noopReply;
    Signature signature {0x01, 0x02, 0x03};
    /// Every system-clock change the SDK asked for, in Unix seconds; none is ever made.
    std::vector<std::int64_t> clockSets;
};

/// A failed transient first attempt, then the endpoint recovers.
void checkTransientThenRecovers(int status)
{
    AuthHarness h;
    h.replies({response(status)});
    h.run();

    CHECK(h.status() == AuthStatus::NotAuthenticated);
    CHECK(NetTestAccess::authFailures(h.net) == 0);
    auto retry = h.takeRetry();
    CHECK(retry.kind == AuthRetryKind::Transient);
    CHECK(retry.delay == 2s);

    retry.fire();
    CHECK(h.net.authenticateCalls == 1);

    const auto token = jwt(1h);
    h.replies({tokenReply(token)});
    h.run();
    CHECK(h.status() == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(NetTestAccess::token(h.net) == token);
    CHECK(h.retries.empty());
}

struct FlakyResolver : IKeyResolver {
    explicit FlakyResolver(std::shared_ptr<bool> available)
        : available(std::move(available))
    {
    }

    bool tryResolve(DeviceInfo &info, const std::string &) override
    {
        info.uuid = "0b2f5c1e-3d4a-4b6c-8e9f-a1b2c3d4e5f6"; // a resolver always supplies one
        return *available;
    }

    SignerCallback createSigner() const override
    {
        return [](const Digest &) { return Signature {0x01}; };
    }

    std::shared_ptr<bool> available;
};

} // namespace

// ------------------------------- transient token endpoint failures ------------------------------

TEST_CASE("A 503 from the token endpoint is retried and recovers without a restart")
{
    checkTransientThenRecovers(503);
}

TEST_CASE("A non-standard 5xx from the token endpoint is retried like a 503")
{
    checkTransientThenRecovers(525);
}

TEST_CASE("Other transient token endpoint failures are retried on the backoff")
{
    for (const auto status : {0, 408, 429, 500, 502, 504}) {
        INFO("status " << status);
        checkTransientThenRecovers(status);
    }
}

TEST_CASE("A 200 the device cannot read is retried, not reported as a failure")
{
    for (const auto *body : {"<html>oops</html>", "{}", "{\"token\": 7}"}) {
        INFO("body " << body);
        AuthHarness h;
        h.replies({response(200, body)});
        h.run();

        CHECK(h.status() == AuthStatus::NotAuthenticated);
        CHECK(NetTestAccess::authFailures(h.net) == 0);
        const auto retry = h.takeRetry();
        CHECK(retry.kind == AuthRetryKind::Transient);
        CHECK(retry.delay == 2s);
    }
}

TEST_CASE("A 400 that outlasts the timestamp retries is retried later")
{
    AuthHarness h;
    h.replies({response(400)});
    h.run();

    CHECK(h.tokenCalls == 11);
    CHECK(h.status() == AuthStatus::NotAuthenticated);
    const auto retry = h.takeRetry();
    CHECK(retry.kind == AuthRetryKind::Transient);
    CHECK(retry.delay == 2s);
}

TEST_CASE("A 404 that cannot be re-provisioned is retried later")
{
    AuthHarness h;
    h.replies({response(404)});
    h.run();

    CHECK(h.status() == AuthStatus::NotAuthenticated);
    CHECK(h.takeRetry().kind == AuthRetryKind::Transient);
}

// ----------------------------------------- Retry-After ------------------------------------------

TEST_CASE("A 429 honours Retry-After in delta-seconds")
{
    AuthHarness h;
    h.replies({response(429, {}, {{"Retry-After", "120"}})});
    h.run();

    const auto retry = h.takeRetry();
    CHECK(retry.kind == AuthRetryKind::Transient);
    CHECK(retry.delay == 120s);
    CHECK(h.status() == AuthStatus::NotAuthenticated);
}

TEST_CASE("A 429 honours Retry-After as an HTTP-date, measured against the server's Date")
{
    // A server clock an hour off the device's: only the server's own Date makes 300s come out.
    const auto serverNow = system_clock::now() + 1h;
    AuthHarness h;
    h.replies({response(
            429, {},
            {{"Date", httpDate(serverNow)}, {"Retry-After", httpDate(serverNow + 300s)}})});
    h.run();

    CHECK(h.takeRetry().delay == 300s);
}

TEST_CASE("A 503 honours Retry-After too")
{
    AuthHarness h;
    h.replies({response(503, {}, {{"Retry-After", "45"}})});
    h.run();

    CHECK(h.takeRetry().delay == 45s);
}

TEST_CASE("An unreadable Retry-After falls back to the backoff")
{
    for (const auto *value : {"soon", "", "-5", "12abc"}) {
        INFO("Retry-After " << value);
        AuthHarness h;
        h.replies({response(429, {}, {{"Retry-After", value}})});
        h.run();

        CHECK(h.takeRetry().delay == 2s);
    }
}

TEST_CASE("Retry-After never shortens the backoff and is capped at an hour")
{
    SECTION("shorter than the backoff")
    {
        AuthHarness h;
        h.replies({response(429, {}, {{"Retry-After", "0"}})});
        h.run();
        CHECK(h.takeRetry().delay == 2s);
    }

    SECTION("absurdly long")
    {
        AuthHarness h;
        h.replies({response(429, {}, {{"Retry-After", "999999"}})});
        h.run();
        CHECK(h.takeRetry().delay == 1h);
    }

    SECTION("the cap holds after jitter too")
    {
        AuthHarness h;
        h.jitter = 0.999;
        h.replies({response(429, {}, {{"Retry-After", "999999"}})});
        h.run();
        CHECK(h.takeRetry().delay == 1h);
    }
}

// ------------------------------------ credential rejection --------------------------------------

TEST_CASE("Rejected credentials are reported and re-checked slowly, never stranded")
{
    for (const auto status : {401, 403, 418, 422}) {
        INFO("status " << status);
        AuthHarness h;
        h.replies({response(status)});
        h.run();

        CHECK(h.status() == AuthStatus::AuthenticationFailed);
        CHECK(NetTestAccess::authFailures(h.net) == 1);
        auto retry = h.takeRetry();
        CHECK(retry.kind == AuthRetryKind::Recheck);
        CHECK(retry.delay == 15min);

        retry.fire();
        CHECK(h.status() == AuthStatus::NotAuthenticated);
        CHECK(h.net.authenticateCalls == 1);

        h.replies({tokenReply(jwt(1h))});
        h.run();
        CHECK(h.status() == AuthStatus::AuthenticatedCheckingPairing);
    }
}

TEST_CASE("A re-check does not advance the transient backoff")
{
    AuthHarness h;
    h.replies({response(401)});
    h.run();
    h.takeRetry().fire();

    h.replies({response(503)});
    h.run();
    CHECK(h.takeRetry().delay == 2s);
}

// ---------------------------------------- token refresh -----------------------------------------

TEST_CASE("A transient failure during a token refresh keeps the session alive")
{
    AuthHarness h;
    NetTestAccess::startTokenRefresh(h.net, AuthStatus::AuthenticatedPaired, jwt(5min));
    h.replies({response(503)});
    h.run();

    CHECK(h.status() == AuthStatus::AuthenticatedPaired);
    CHECK(NetTestAccess::authFailures(h.net) == 0); // so the heartbeat was not stopped
    CHECK_FALSE(NetTestAccess::isRefreshingToken(h.net));
    auto retry = h.takeRetry();
    CHECK(retry.kind == AuthRetryKind::Transient);
    CHECK(retry.delay == 2s);

    retry.fire();
    CHECK(h.net.authenticateCalls == 1);
    CHECK(NetTestAccess::isRefreshingToken(h.net));

    const auto token = jwt(1h);
    h.replies({tokenReply(token)});
    h.run();
    CHECK(h.status() == AuthStatus::AuthenticatedPaired);
    CHECK(NetTestAccess::token(h.net) == token);
    CHECK(h.retries.empty());
}

TEST_CASE("A refresh retry is never scheduled past the token's expiry")
{
    AuthHarness h;
    NetTestAccess::startTokenRefresh(h.net, AuthStatus::AuthenticatedUnpaired, jwt(10min));
    h.replies({response(429, {}, {{"Retry-After", "3600"}})});
    h.run();

    const auto delay = h.takeRetry().delay;
    CHECK(delay <= 10min);
    CHECK(delay >= 9min);
    CHECK(h.status() == AuthStatus::AuthenticatedUnpaired);
}

TEST_CASE("A refresh that cannot finish before the token expires falls back to a normal one")
{
    AuthHarness h;
    NetTestAccess::startTokenRefresh(h.net, AuthStatus::AuthenticatedPaired, jwt(-1min));
    h.replies({response(503)});
    h.run();

    auto retry = h.takeRetry();
    CHECK(retry.delay == 0s);

    retry.fire();
    CHECK(h.status() == AuthStatus::NotAuthenticated);
    CHECK_FALSE(NetTestAccess::isRefreshingToken(h.net));
    CHECK(h.net.authenticateCalls == 1);

    h.replies({tokenReply(jwt(1h))});
    h.run();
    CHECK(h.status() == AuthStatus::AuthenticatedCheckingPairing);
}

TEST_CASE("A rejection during a token refresh keeps the session until the token expires")
{
    for (const auto status : {401, 403, 418}) {
        INFO("status " << status);
        AuthHarness h;
        NetTestAccess::startTokenRefresh(h.net, AuthStatus::AuthenticatedPaired, jwt(5min));
        h.replies({response(status)});
        h.run();

        CHECK(h.status() == AuthStatus::AuthenticatedPaired);
        CHECK(NetTestAccess::authFailures(h.net) == 0); // so the heartbeat was not stopped
        auto retry = h.takeRetry();
        CHECK(retry.kind == AuthRetryKind::Recheck);
        CHECK(retry.delay <= 5min);
        CHECK(retry.delay >= 4min);

        retry.fire();
        CHECK(NetTestAccess::isRefreshingToken(h.net));
        CHECK(h.net.authenticateCalls == 1);
    }
}

// ---------------------------------------- empty signature ---------------------------------------

TEST_CASE("An empty signature during a token refresh keeps the session alive")
{
    AuthHarness h;
    h.signature.clear();
    NetTestAccess::startTokenRefresh(h.net, AuthStatus::AuthenticatedPaired, jwt(5min));
    h.run();

    CHECK(h.tokenCalls == 0);
    CHECK(h.status() == AuthStatus::AuthenticatedPaired);
    CHECK(NetTestAccess::authFailures(h.net) == 0);
    auto retry = h.takeRetry();
    CHECK(retry.delay == 2s);

    retry.fire();
    CHECK(NetTestAccess::isRefreshingToken(h.net));
    CHECK(h.net.authenticateCalls == 1);
}

TEST_CASE("An empty signature on a first authentication is still reported, and retried")
{
    AuthHarness h;
    h.signature.clear();
    h.run();

    CHECK(h.status() == AuthStatus::AuthenticationFailed);
    auto retry = h.takeRetry();
    CHECK(retry.kind == AuthRetryKind::SignerFailure);
    CHECK(retry.delay == 2s);

    retry.fire();
    CHECK(h.status() == AuthStatus::NotAuthenticated);
    CHECK(h.net.authenticateCalls == 1);
}

// ---------------------------------------- key resolution ----------------------------------------

TEST_CASE("No usable key is reported and re-checked until one appears")
{
    const auto available = std::make_shared<bool>(false);
    std::vector<std::unique_ptr<IKeyResolver>> resolvers;
    resolvers.push_back(std::make_unique<FlakyResolver>(available));
    AuthHarness h {std::move(resolvers), false};
    h.replies({tokenReply(jwt(1h))});
    h.run();

    CHECK(h.tokenCalls == 0);
    CHECK(h.status() == AuthStatus::AuthenticationFailed);
    auto retry = h.takeRetry();
    CHECK(retry.kind == AuthRetryKind::Recheck);
    CHECK(retry.delay == 15min);

    *available = true;
    retry.fire();
    CHECK(h.status() == AuthStatus::NotAuthenticated);
    h.run();
    CHECK(h.status() == AuthStatus::AuthenticatedCheckingPairing);
}

TEST_CASE("No resolver and no signer is a key failure, not a crash")
{
    AuthHarness h {{}, false};
    h.run();

    CHECK(h.status() == AuthStatus::AuthenticationFailed);
    CHECK(h.takeRetry().kind == AuthRetryKind::Recheck);
}

// --------------------------------------------- stop ---------------------------------------------

TEST_CASE("Nothing is rescheduled once stopping")
{
    SECTION("transient failure")
    {
        AuthHarness h;
        NetTestAccess::setStopping(h.net);
        h.replies({response(503)});
        h.run();
        CHECK(h.retries.empty());
    }

    SECTION("no usable key")
    {
        AuthHarness h {{}, false};
        NetTestAccess::setStopping(h.net);
        h.run();
        CHECK(h.retries.empty());
    }

    SECTION("empty signature")
    {
        AuthHarness h;
        h.signature.clear();
        NetTestAccess::setStopping(h.net);
        h.run();
        CHECK(h.retries.empty());
    }

    SECTION("a retry that fires after stopping does not authenticate")
    {
        AuthHarness h;
        h.replies({response(503)});
        h.run();
        auto retry = h.takeRetry();
        NetTestAccess::setStopping(h.net);
        retry.fire();
        CHECK(h.net.authenticateCalls == 0);
    }
}

// -------------------------------------------- backoff -------------------------------------------

TEST_CASE("A device that gets no reply backs off to one minute")
{
    AuthHarness h;
    h.replies({response(0)});
    for (const auto expected : {2s, 4s, 8s, 16s, 32s, 60s, 60s}) {
        h.run();
        CHECK(h.takeRetry().delay == expected);
    }
}

TEST_CASE("A failure of the other class is held to its own cap after server errors raised it")
{
    AuthHarness h;
    h.replies({response(503)});
    for (int i = 0; i < 9; ++i) { // backoff now at the 5 min server-error cap
        h.run();
        h.takeRetry();
    }

    h.replies({response(0)}); // no reply: capped at 1 min, now as well as next time
    h.run();
    CHECK(h.takeRetry().delay == 60s);
    h.run();
    CHECK(h.takeRetry().delay == 60s);
}

TEST_CASE("The auth backoff after server errors doubles to five minutes and resets on success")
{
    AuthHarness h;
    h.replies({response(503)});
    for (const auto expected : {2s, 4s, 8s, 16s, 32s, 64s, 128s, 256s, 300s, 300s}) {
        h.run();
        CHECK(h.takeRetry().delay == expected);
    }

    h.replies({tokenReply(jwt(1h))});
    h.run();
    REQUIRE(h.status() == AuthStatus::AuthenticatedCheckingPairing);

    NetTestAccess::startTokenRefresh(h.net, AuthStatus::AuthenticatedPaired, jwt(1h));
    h.replies({response(503)});
    h.run();
    CHECK(h.takeRetry().delay == 2s);
}

TEST_CASE("Retry delays are jittered so a fleet that failed together spreads out")
{
    SECTION("a backoff delay is drawn from its lower half up to the full delay")
    {
        AuthHarness h;
        h.replies({response(503)});
        for (int i = 0; i < 9; ++i) { // reach the 5 min server-error cap
            h.run();
            h.takeRetry();
        }
        h.jitter = 0.5;
        h.run();
        CHECK(h.takeRetry().delay == 225s); // 300 s - 300 s * 0.5 * 0.5

        h.jitter = 0.999;
        h.run();
        const auto delay = h.takeRetry().delay;
        CHECK(delay >= 150s);
        CHECK(delay <= 300s);
    }

    SECTION("even the first, 2 s retry is spread")
    {
        AuthHarness h;
        h.jitter = 0.999;
        h.replies({response(503)});
        h.run();
        CHECK(h.takeRetry().delay == 1s); // 2 s - round(2 s * 0.5 * 0.999)
    }

    SECTION("a Retry-After is never shortened, only spread later")
    {
        AuthHarness h;
        h.jitter = 0.5;
        h.replies({response(429, {}, {{"Retry-After", "120"}})});
        h.run();
        CHECK(h.takeRetry().delay == 135s); // 120 s + 120 s * 0.25 * 0.5
    }

    SECTION("the slow re-check is spread over its last fifth")
    {
        AuthHarness h;
        h.jitter = 0.5;
        h.replies({response(401)});
        h.run();
        CHECK(h.takeRetry().delay == 810s); // 900 s - 900 s * 0.2 * 0.5
    }
}

// ------------------------------- strand, blocking and crash paths -------------------------------

namespace {

// Test keys only, as in test_soft_key_resolver.cpp.
constexpr auto TEST_DEVICE_KEY_HEX =
        "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2";
constexpr auto TEST_PROVIDER_KEY_HEX =
        "f1e2d3c4b5a69788796a5b4c3d2e1f00f1e2d3c4b5a69788796a5b4c3d2e1f00";
constexpr auto TEST_PROVIDER = "testprovider";
constexpr auto TEST_UUID = "c7f1fd0b-82f7-5504-8fbe-740c09bc7dab";

std::vector<uint8_t> hexToBytes(const std::string &hex)
{
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

/// A saved soft key, as SoftKeyResolver stores it, carrying @p uuid.
std::string savedKeyJson(const std::string &uuid)
{
    const auto providerKey = hexToBytes(TEST_PROVIDER_KEY_HEX);
    const auto password = utils::ByteArray(providerKey.data(), providerKey.size()).hex();
    const auto encKey = encryptSecret(hexToBytes(TEST_DEVICE_KEY_HEX), password);
    const auto hmac = computeHmac(std::string(TEST_PROVIDER) + uuid + "1" + encKey, password);
    return nlohmann::json {{"encrypted_key", encKey},
                           {"uuid", uuid},
                           {"serial_number", 1},
                           {"provider", TEST_PROVIDER},
                           {"hmac", hmac}}
            .dump();
}

/// Soft-key provisioning whose saved key is whatever @p savedKey holds. Machine id 0 fails device
/// validation on purpose, so the worker still never starts.
DeviceInfo softKeyDeviceInfo(std::shared_ptr<std::string> savedKey)
{
    DeviceInfo info;
    info.provider = TEST_PROVIDER;
    info.encryptedKey = encryptSecret(hexToBytes(TEST_PROVIDER_KEY_HEX),
                                      std::string(TEST_PROVIDER) + SCORBIT_SDK_ENCRYPT_SECRET);
    info.saveKeyCallback = [](const std::string &) {};
    info.loadKeyCallback = [savedKey] { return *savedKey; };
    return info;
}

std::string requestUuid(const std::string &tokenBody)
{
    return nlohmann::json::parse(tokenBody).at("uuid").get<std::string>();
}

struct UuidResolver : IKeyResolver {
    UuidResolver(std::shared_ptr<std::string> uuid, std::shared_ptr<int> calls)
        : uuid(std::move(uuid))
        , calls(std::move(calls))
    {
    }

    bool tryResolve(DeviceInfo &info, const std::string &) override
    {
        ++*calls;
        info.uuid = *uuid;
        return true;
    }

    SignerCallback createSigner() const override
    {
        return [](const Digest &) { return Signature {0x01}; };
    }

    std::shared_ptr<std::string> uuid;
    std::shared_ptr<int> calls;
};

} // namespace

TEST_CASE("A failed re-provision after a 404 leaves a resolver for the next attempt")
{
    // The saved key's uuid is unusable, so re-provisioning fails after the stale key and the
    // signer are already gone -- without reaching the provisioning server.
    const auto savedKey = std::make_shared<std::string>(savedKeyJson("not-a-uuid"));
    AuthHarness h {{}, true, softKeyDeviceInfo(savedKey)};
    h.replies({response(404)});
    h.run();

    CHECK(h.tokenCalls == 1);
    CHECK(h.status() == AuthStatus::NotAuthenticated);
    auto retry = h.takeRetry();
    CHECK(retry.kind == AuthRetryKind::Transient);

    *savedKey = savedKeyJson(TEST_UUID);
    retry.fire();
    h.replies({tokenReply(jwt(1h))});
    h.run();

    CHECK(h.status() == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(requestUuid(h.lastTokenBody) == parseUuid(TEST_UUID));
}

namespace {

struct HardwareUuidResolver : UuidResolver {
    using UuidResolver::UuidResolver;
    bool isHardwareKey() const override { return true; }
};

/// A 404 on the token endpoint with soft-key provisioning configured; returns the saved-key writes.
int savesAfter404(std::unique_ptr<IKeyResolver> resolver,
                  std::shared_ptr<const HardwareKeyMarker> marker = {})
{
    auto info = softKeyDeviceInfo(std::make_shared<std::string>(savedKeyJson(TEST_UUID)));
    const auto saves = std::make_shared<int>(0);
    info.saveKeyCallback = [saves](const std::string &) { ++*saves; };
    // With no resolver, the device already holds a signer from elsewhere.
    const bool withSigner = !resolver;
    std::vector<std::unique_ptr<IKeyResolver>> resolvers;
    if (resolver) {
        resolvers.push_back(std::move(resolver));
    }
    AuthHarness h {std::move(resolvers), withSigner, std::move(info), std::move(marker)};
    h.replies({response(404)});
    h.run();
    CHECK(h.takeRetry().kind == AuthRetryKind::Transient);
    return *saves;
}

} // namespace

TEST_CASE("SB-6133: a 404 never re-provisions a soft key over a hardware key")
{
    const auto uuid = std::make_shared<std::string>(TEST_UUID);
    const auto calls = std::make_shared<int>(0);

    SECTION("control: a soft key is re-provisioned, clearing the saved key")
    {
        CHECK(savesAfter404(std::make_unique<UuidResolver>(uuid, calls)) > 0);
    }
    SECTION("the active key came from the tap pad's TPM")
    {
        CHECK(savesAfter404(std::make_unique<HardwareUuidResolver>(uuid, calls)) == 0);
    }
    SECTION("a hardware-key marker exists")
    {
        std::random_device rd;
        const auto dir = std::filesystem::temp_directory_path()
                       / ("scorbit-hwkey-404-" + std::to_string(rd()));
        const auto marker =
                std::make_shared<HardwareKeyMarker>(std::vector<std::filesystem::path> {dir});
        REQUIRE(marker->store({1, "0123456789abcdef0123456789abcdef"}));
        CHECK(savesAfter404(nullptr, marker) == 0);
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
}

namespace {

struct MarkerDir {
    MarkerDir()
    {
        std::random_device rd;
        path = std::filesystem::temp_directory_path()
             / ("scorbit-hwkey-auth-" + std::to_string(rd()));
        marker = std::make_shared<HardwareKeyMarker>(std::vector<std::filesystem::path> {path});
    }
    ~MarkerDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::filesystem::path path;
    std::shared_ptr<HardwareKeyMarker> marker;
};

} // namespace

TEST_CASE("SB-6133: the marker records a hardware key only once the api accepts it")
{
    const auto uuid = std::make_shared<std::string>(TEST_UUID);
    const auto calls = std::make_shared<int>(0);
    MarkerDir dir;
    DeviceInfo info;
    info.serialNumber = 4321;

    SECTION("accepted")
    {
        std::vector<std::unique_ptr<IKeyResolver>> resolvers;
        resolvers.push_back(std::make_unique<HardwareUuidResolver>(uuid, calls));
        AuthHarness h {std::move(resolvers), false, info, dir.marker};
        h.replies({tokenReply(jwt(1h))});
        h.run();
        REQUIRE(h.status() == AuthStatus::AuthenticatedCheckingPairing);
        CHECK(dir.marker->load() == HardwareKeyIdentity {4321, parseUuid(TEST_UUID)});
    }
    SECTION("rejected")
    {
        std::vector<std::unique_ptr<IKeyResolver>> resolvers;
        resolvers.push_back(std::make_unique<HardwareUuidResolver>(uuid, calls));
        AuthHarness h {std::move(resolvers), false, info, dir.marker};
        h.replies({response(401)});
        h.run();
        CHECK(h.status() != AuthStatus::AuthenticatedCheckingPairing);
        CHECK_FALSE(dir.marker->load());
    }
#if !defined(_WIN32) // relies on POSIX permissions
    SECTION("a marker that could not be stored is stored at the next token refresh")
    {
        // The marker's parent is writable by others at first, so no store is trusted.
        const auto parent = dir.path;
        const auto markerDir = parent / "scorbit";
        std::filesystem::create_directories(markerDir);
        std::filesystem::permissions(parent, std::filesystem::perms::others_write,
                                     std::filesystem::perm_options::add);
        const auto marker =
                std::make_shared<HardwareKeyMarker>(std::vector<std::filesystem::path> {markerDir});

        std::vector<std::unique_ptr<IKeyResolver>> resolvers;
        resolvers.push_back(std::make_unique<HardwareUuidResolver>(uuid, calls));
        AuthHarness h {std::move(resolvers), false, info, marker};
        h.replies({tokenReply(jwt(1h))});
        h.run();
        REQUIRE(h.status() == AuthStatus::AuthenticatedCheckingPairing);
        REQUIRE_FALSE(marker->load());

        std::filesystem::permissions(parent, std::filesystem::perms::others_write,
                                     std::filesystem::perm_options::remove);
        NetTestAccess::startTokenRefresh(h.net, AuthStatus::AuthenticatedPaired, jwt(5min));
        h.replies({tokenReply(jwt(1h))});
        h.run();
        CHECK(marker->load() == HardwareKeyIdentity {4321, parseUuid(TEST_UUID)});
    }
#endif
    SECTION("a soft key, accepted, is never recorded")
    {
        std::vector<std::unique_ptr<IKeyResolver>> resolvers;
        resolvers.push_back(std::make_unique<UuidResolver>(uuid, calls));
        AuthHarness h {std::move(resolvers), false, info, dir.marker};
        h.replies({tokenReply(jwt(1h))});
        h.run();
        REQUIRE(h.status() == AuthStatus::AuthenticatedCheckingPairing);
        CHECK_FALSE(dir.marker->load());
    }
}

TEST_CASE("A resolver's bad uuid commits no signer and keeps the resolvers for the re-check")
{
    const auto uuid = std::make_shared<std::string>("not-a-uuid");
    const auto calls = std::make_shared<int>(0);
    std::vector<std::unique_ptr<IKeyResolver>> resolvers;
    resolvers.push_back(std::make_unique<UuidResolver>(uuid, calls));
    AuthHarness h {std::move(resolvers), false};
    h.replies({tokenReply(jwt(1h))});
    h.run();

    CHECK(h.tokenCalls == 0);
    CHECK(h.status() == AuthStatus::AuthenticationFailed);
    auto retry = h.takeRetry();
    CHECK(retry.kind == AuthRetryKind::Recheck);

    *uuid = TEST_UUID;
    retry.fire();
    h.run();

    CHECK(*calls == 2);
    CHECK(h.status() == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(requestUuid(h.lastTokenBody) == parseUuid(TEST_UUID));
}

TEST_CASE("A retry that finds the lock busy re-arms itself instead of waiting or dropping")
{
    AuthHarness h;
    h.replies({response(401)});
    h.run();
    auto retry = h.takeRetry();

    {
        const auto held = NetTestAccess::holdAuthLock(h.net);
        std::thread timer {retry.fire}; // the timer runs on another thread
        timer.join();                   // returns at once: it did not wait for the lock
    }
    CHECK(h.net.authenticateCalls == 0);
    CHECK(h.status() == AuthStatus::AuthenticationFailed);

    // Not dropped: a brief holder may never start an authentication, so the retry comes back.
    auto rearmed = h.takeRetry();
    CHECK(rearmed.kind == retry.kind);
    CHECK(rearmed.delay == 2s);

    rearmed.fire();
    CHECK(h.net.authenticateCalls == 1);
    CHECK(h.status() == AuthStatus::NotAuthenticated);
}

TEST_CASE("An exception escaping the authentication task is retried, not fatal")
{
    SECTION("std::exception on a first authentication")
    {
        AuthHarness h;
        h.replies({response(200)});
        h.beforeToken = [] { throw std::runtime_error("transport blew up"); };
        h.run();

        CHECK(h.status() == AuthStatus::NotAuthenticated);
        CHECK(h.takeRetry().kind == AuthRetryKind::Transient);
    }

    SECTION("anything else on a first authentication")
    {
        AuthHarness h;
        h.replies({response(200)});
        h.beforeToken = [] { throw 42; };
        h.run();

        CHECK(h.status() == AuthStatus::NotAuthenticated);
        CHECK(h.takeRetry().kind == AuthRetryKind::Transient);
    }

    SECTION("during a token refresh the session is kept")
    {
        AuthHarness h;
        NetTestAccess::startTokenRefresh(h.net, AuthStatus::AuthenticatedPaired, jwt(5min));
        h.replies({response(200)});
        h.beforeToken = [] { throw std::runtime_error("transport blew up"); };
        h.run();

        CHECK(h.status() == AuthStatus::AuthenticatedPaired);
        CHECK(h.takeRetry().delay == 2s);
    }

    SECTION("nothing is scheduled once stopping")
    {
        AuthHarness h;
        h.replies({response(200)});
        h.beforeToken = [&h] {
            NetTestAccess::setStopping(h.net);
            throw std::runtime_error("transport blew up");
        };
        h.run();

        CHECK(h.retries.empty());
    }
}

TEST_CASE("A successful authentication cancels a retry still pending")
{
    AuthHarness h;
    h.replies({response(503)});
    h.run();
    REQUIRE(h.retries.size() == 1);

    // An authentication started some other way (a request's 401) succeeds before it fires.
    h.replies({tokenReply(jwt(1h))});
    h.run();

    CHECK(h.status() == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(h.timerStops == 1);
    CHECK(h.retries.empty());
}

// ---------------------------------- classification review fixes ---------------------------------

TEST_CASE("A Retry-After of any number of digits is read, saturating at the cap")
{
    for (const auto *value : {"4294967295", "1234567890123456789012345"}) {
        INFO("Retry-After " << value);
        AuthHarness h;
        h.replies({response(429, {}, {{"Retry-After", value}})});
        h.run();

        CHECK(h.takeRetry().delay == 1h);
    }
}

TEST_CASE("A resolver that reports success with no uuid is a key failure")
{
    const auto uuid = std::make_shared<std::string>();
    const auto calls = std::make_shared<int>(0);
    std::vector<std::unique_ptr<IKeyResolver>> resolvers;
    resolvers.push_back(std::make_unique<UuidResolver>(uuid, calls));
    AuthHarness h {std::move(resolvers), false};
    h.replies({tokenReply(jwt(1h))});
    h.run();

    CHECK(h.tokenCalls == 0);
    CHECK(h.status() == AuthStatus::AuthenticationFailed);
    CHECK(NetTestAccess::keyResolverCount(h.net) == 1);
    CHECK(h.takeRetry().kind == AuthRetryKind::Recheck);
}

TEST_CASE("A re-provision that throws still leaves a resolver to provision with")
{
    const auto savedKey = std::make_shared<std::string>(savedKeyJson(TEST_UUID));
    const auto loads = std::make_shared<int>(0);
    const auto saveThrows = std::make_shared<bool>(true);
    auto info = softKeyDeviceInfo(savedKey);
    info.saveKeyCallback = [saveThrows](const std::string &) {
        if (*saveThrows) {
            throw std::runtime_error("storage unavailable");
        }
    };
    info.loadKeyCallback = [savedKey, loads] {
        ++*loads;
        return *savedKey;
    };

    AuthHarness h {{}, true, std::move(info)};
    h.replies({response(404)});
    h.run();

    CHECK(NetTestAccess::keyResolverCount(h.net) == 1);
    auto retry = h.takeRetry();
    CHECK(retry.kind == AuthRetryKind::Transient);
    CHECK(*loads == 0);

    *saveThrows = false;
    retry.fire();
    h.replies({tokenReply(jwt(1h))});
    h.run();

    CHECK(*loads == 1); // the restored soft-key resolver ran
    CHECK(h.status() == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(requestUuid(h.lastTokenBody) == parseUuid(TEST_UUID));
}

TEST_CASE("An unexpected non-error status is retried, not taken as a rejection")
{
    for (const auto status : {204, 302, 101, 600}) {
        INFO("status " << status);
        AuthHarness h;
        h.replies({response(status)});
        h.run();

        CHECK(h.status() == AuthStatus::NotAuthenticated);
        CHECK(NetTestAccess::authFailures(h.net) == 0);
        CHECK(h.takeRetry().kind == AuthRetryKind::Transient);
    }
}

TEST_CASE("Retry-After is honoured on every transient failure")
{
    for (const auto status : {502, 500, 408, 0}) {
        INFO("status " << status);
        AuthHarness h;
        h.replies({response(status, {}, {{"Retry-After", "120"}})});
        h.run();

        CHECK(h.takeRetry().delay >= 120s);
    }
}

TEST_CASE("A token that is malformed, expired or about to expire is retried, not accepted")
{
    // 2s is the shortest refresh delay: a token with no more left would expire before refreshing.
    for (const auto &token : {jwt(-1min), jwt(1s), jwt(2s), std::string {"not-a-jwt"}}) {
        INFO("token " << token);
        AuthHarness h;
        h.replies({tokenReply(token)});
        h.run();

        CHECK(h.status() == AuthStatus::NotAuthenticated);
        CHECK(NetTestAccess::token(h.net).empty());
        CHECK(h.takeRetry().kind == AuthRetryKind::Transient);
    }
}

TEST_CASE("A refresh that gets an unusable token keeps the one it has")
{
    AuthHarness h;
    const auto current = jwt(1h);
    NetTestAccess::startTokenRefresh(h.net, AuthStatus::AuthenticatedPaired, current);
    h.replies({tokenReply(jwt(-1min))});
    h.run();

    CHECK(NetTestAccess::token(h.net) == current);
    CHECK(h.status() == AuthStatus::AuthenticatedPaired);
    CHECK(h.takeRetry().kind == AuthRetryKind::Transient);
}

TEST_CASE("A token shorter-lived than the refresh margin is still refreshed before it expires")
{
    CHECK(NetTestAccess::tokenRefreshDelay(1h) == 55min);
    CHECK(NetTestAccess::tokenRefreshDelay(4min) == 2min);
    CHECK(NetTestAccess::tokenRefreshDelay(2s) == 2s);
    CHECK_FALSE(NetTestAccess::tokenRefreshDelay(0s));
    CHECK_FALSE(NetTestAccess::tokenRefreshDelay(std::nullopt));

    AuthHarness h;
    h.replies({tokenReply(jwt(2min))});
    h.run();
    CHECK(h.status() == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(h.retries.empty());
    const auto armed = NetTestAccess::tokenRefreshArmedIn(h.net);
    REQUIRE(armed);
    CHECK(*armed > 0s);
    CHECK(*armed <= 1min);
}

TEST_CASE("A token is judged against the server's clock, not a device clock that runs ahead")
{
    // The device is two hours ahead: a token the server issued for one hour has already
    // "expired" by the local clock, but not by the server's.
    const auto serverNow = system_clock::now() - 2h;
    const auto token = jwt(-1h);
    AuthHarness h;
    h.replies({response(200, nlohmann::json {{"token", token}}.dump(),
                        {{"Date", httpDate(serverNow)}})});
    h.run();

    CHECK(h.status() == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(NetTestAccess::token(h.net) == token);
    const auto armed = NetTestAccess::tokenRefreshArmedIn(h.net);
    REQUIRE(armed);
    CHECK(*armed > 50min);
}

TEST_CASE("A failed refresh keeps the server-judged lifetime on a device clock that runs ahead")
{
    // Accepted with an hour left by the server's clock, though "expired" by the device's.
    AuthHarness h;
    h.replies({response(200, nlohmann::json {{"token", jwt(-1h)}}.dump(),
                        {{"Date", httpDate(system_clock::now() - 2h)}})});
    h.run();
    REQUIRE(h.status() == AuthStatus::AuthenticatedCheckingPairing);
    REQUIRE(NetTestAccess::heartbeatOn(h.net));

    // Its refresh fails: the retry follows the backoff, not a clock that says the token is gone,
    // and when it runs the session is still treated as live.
    NetTestAccess::markRefreshing(h.net);
    h.replies({response(503)});
    h.run();
    auto retry = h.takeRetry();
    CHECK(retry.delay == 2s);

    retry.fire();
    CHECK(h.status() == AuthStatus::AuthenticatedCheckingPairing);
    CHECK(NetTestAccess::isRefreshingToken(h.net));
    CHECK(NetTestAccess::heartbeatOn(h.net));
    CHECK(h.net.authenticateCalls == 1);
}

TEST_CASE("A token that expires while a refresh is retried stops the heartbeat")
{
    AuthHarness h;
    h.replies({tokenReply(jwt(1h))});
    h.run();
    REQUIRE(NetTestAccess::heartbeatOn(h.net));

    NetTestAccess::startTokenRefresh(h.net, AuthStatus::AuthenticatedPaired, jwt(-1min));
    h.replies({response(503)});
    h.run();
    CHECK(NetTestAccess::heartbeatOn(h.net)); // a failing refresh keeps its session meanwhile

    h.takeRetry().fire();
    CHECK(h.status() == AuthStatus::NotAuthenticated);
    CHECK_FALSE(NetTestAccess::heartbeatOn(h.net));
}

TEST_CASE("Centrifugo rejecting the token as expired drops the cached one and fetches another")
{
    // Without this the client's reconnect asks getToken again and is handed the same expired
    // token every time.
    AuthHarness h;
    NetTestAccess::setCentrifugoToken(h.net, "expired-token");
    NetTestAccess::onCentrifugoError(
            h.net, {make_error_code(centrifugo::ErrorType::TokenExpired), "token expired"});

    CHECK(NetTestAccess::cachedCentrifugoToken(h.net).empty());
    // The harness's Net never starts its worker (its DeviceInfo does not validate), so the posted
    // fetch stays queued and the flag stays set.
    CHECK(NetTestAccess::centrifugoTokenFetchInFlight(h.net));
}

TEST_CASE("Other Centrifugo errors keep the cached token")
{
    AuthHarness h;
    NetTestAccess::setCentrifugoToken(h.net, "good-token");
    NetTestAccess::onCentrifugoError(
            h.net, {make_error_code(centrifugo::ErrorType::PermissionDenied), "permission denied"});

    CHECK(NetTestAccess::cachedCentrifugoToken(h.net) == "good-token");
    CHECK_FALSE(NetTestAccess::centrifugoTokenFetchInFlight(h.net));
}

TEST_CASE("A Centrifugo token's refresh is timed by the server's clock, not the device's")
{
    // The server issues a 30 min token; the refresh is due 26 min out (3 min before the client's
    // own request, plus a minute of headroom) however far off the device clock is.
    const auto now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    const auto armedFor = [](std::int64_t serverNow, seconds lifeByLocalClock) {
        AuthHarness h;
        NetTestAccess::scheduleCentrifugoTokenRefresh(h.net, jwt(lifeByLocalClock), serverNow);
        return NetTestAccess::centrifugoTokenRefreshArmedIn(h.net);
    };

    SECTION("a device clock 21 h behind")
    {
        const auto armed = armedFor(now + 21 * 3600, 21h + 30min);
        REQUIRE(armed);
        CHECK(*armed >= 25min);
        CHECK(*armed <= 26min);
    }
    SECTION("a device clock 21 h ahead")
    {
        const auto armed = armedFor(now - 21 * 3600, -21h + 30min);
        REQUIRE(armed);
        CHECK(*armed >= 25min);
        CHECK(*armed <= 26min);
    }
    SECTION("no server time: the local clock, as before")
    {
        const auto armed = armedFor(0, 30min);
        REQUIRE(armed);
        CHECK(*armed >= 25min);
        CHECK(*armed <= 26min);
    }
}

TEST_CASE("The first server Date from any REST reply corrects a wrong clock, once")
{
    // The boot-time server-time request can fail while the network comes up; the next good reply
    // must still correct the clock (SB-5609).
    const auto server = system_clock::now() + 2h;
    const auto serverEpoch = duration_cast<seconds>(server.time_since_epoch()).count();
    const auto atServer = [](system_clock::time_point at) {
        return cpr::Header {{"Date", httpDate(at)}};
    };

    SECTION("a clock 2 h off is set to the server's time")
    {
        AuthHarness h;
        NetTestAccess::noteRestSuccess(h.net, atServer(server));
        REQUIRE(h.clockSets.size() == 1);
        CHECK(h.clockSets.front() == serverEpoch);

        NetTestAccess::noteRestSuccess(h.net, atServer(server + 1h));
        CHECK(h.clockSets.size() == 1); // only the first reading decides
    }
    SECTION("an accurate clock is left alone, and that settles it")
    {
        AuthHarness h;
        NetTestAccess::noteRestSuccess(h.net, atServer(system_clock::now()));
        NetTestAccess::noteRestSuccess(h.net, atServer(server));
        CHECK(h.clockSets.empty());
    }
    SECTION("a cached reply's Date is ignored and does not use up the check")
    {
        AuthHarness h;
        auto cached = atServer(server - 5h);
        cached["Age"] = "18000";
        NetTestAccess::noteRestSuccess(h.net, cached);
        CHECK(h.clockSets.empty());
        NetTestAccess::noteRestSuccess(h.net, atServer(server));
        CHECK(h.clockSets.size() == 1);
    }
    SECTION("an implausible Date is ignored and does not use up the check")
    {
        AuthHarness h;
        NetTestAccess::noteRestSuccess(h.net, atServer(system_clock::from_time_t(1577836800)));
        CHECK(h.clockSets.empty());
        NetTestAccess::noteRestSuccess(h.net, atServer(server));
        CHECK(h.clockSets.size() == 1);
    }
    SECTION("the server-time request before authenticating sets it too")
    {
        AuthHarness h;
        h.noopReply = [&] { return response(200, {}, atServer(server)); };
        h.replies({tokenReply(jwt(1h))});
        h.run();
        REQUIRE(h.clockSets.size() == 1);
        CHECK(h.clockSets.front() == serverEpoch);
    }
    SECTION("but not from a cached reply to it")
    {
        AuthHarness h;
        h.noopReply = [&] {
            auto cached = atServer(server);
            cached["Age"] = "18000";
            return response(200, {}, cached);
        };
        h.replies({tokenReply(jwt(1h))});
        h.run();
        CHECK(h.clockSets.empty());
    }
}
