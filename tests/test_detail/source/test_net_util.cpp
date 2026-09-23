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

#include "net_util.h"
#include "device_info.h"
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using namespace std::chrono_literals;

TEST_CASE("Valid HTTPS URL with port", "[exctractHostAndPort]")
{
    std::string url = "https://example.com:443/";
    UrlInfo result = exctractHostAndPort(url);

    CHECK(result.protocol == "https");
    CHECK(result.hostname == "example.com");
    CHECK(result.port == "443");
}

TEST_CASE("Valid HTTP URL with port", "[exctractHostAndPort]")
{
    std::string url = "http://example.com:8080/";
    UrlInfo result = exctractHostAndPort(url);

    CHECK(result.protocol == "http");
    CHECK(result.hostname == "example.com");
    CHECK(result.port == "8080");
}

TEST_CASE("Valid HTTPS URL without port", "[exctractHostAndPort]")
{
    std::string url = "https://example.com/";
    UrlInfo result = exctractHostAndPort(url);

    CHECK(result.protocol == "https");
    CHECK(result.hostname == "example.com");
    CHECK(result.port == "443"); // Default port for HTTPS
}

TEST_CASE("Valid HTTP URL without port", "[exctractHostAndPort]")
{
    std::string url = "http://example.com/";
    UrlInfo result = exctractHostAndPort(url);

    CHECK(result.protocol == "http");
    CHECK(result.hostname == "example.com");
    CHECK(result.port == "80"); // Default port for HTTP
}

TEST_CASE("Invalid URL", "[exctractHostAndPort]")
{
    std::string url = "ftp://example.com/";
    UrlInfo result = exctractHostAndPort(url);

    CHECK(result.protocol.empty());
    CHECK(result.hostname.empty());
    CHECK(result.port.empty());
}

TEST_CASE("Remove single symbol", "[removeSymbols]")
{
    std::string s {"hello-world"};
    CHECK(removeSymbols(s, "-") == "helloworld");
}

TEST_CASE("Remove few of single symbols", "[removeSymbols]")
{
    std::string s {"=hello=world="};
    CHECK(removeSymbols(s, "=") == "helloworld");
}

TEST_CASE("Remove different symbols", "[removeSymbols]")
{
    std::string s {"{f0b188f8-9f2d-4f8d-abe4-c3107516e7ce}"};
    CHECK(removeSymbols(s, "-{}") == "f0b188f89f2d4f8dabe4c3107516e7ce");
}

TEST_CASE("Derive UUID v5 from given source", "[deriveUuid]")
{
    const auto uuid = deriveUuid("aaa");
    // https://uuidgenerator.dev/uuid-v5 - choose DNS namespace
    CHECK(uuid == "01d2f0ce-8f47-56e4-9a9c-0f368406feb7");

    const auto uuid2 = deriveUuid("52:00:66:74:98:50");
    CHECK(uuid2 == "f4de2fc0-36bf-5209-b019-d40c961d079e");
}

TEST_CASE("Parse UUID", "[parseUuid]")
{
    const auto uuid = parseUuid("f4de2fc0-36bf-5209-b019-d40c961d079e");
    CHECK(uuid == "f4de2fc0-36bf-5209-b019-d40c961d079e");
}

TEST_CASE("Parse UUID with curly braces", "[parseUuid]")
{
    const auto uuid = parseUuid("{f4de2fc0-36bf-5209-b019-d40c961d079e}");
    CHECK(uuid == "f4de2fc0-36bf-5209-b019-d40c961d079e");
}

TEST_CASE("Parse UUID without dashes", "[parseUuid]")
{
    const auto uuid = parseUuid("f4de2fc036bf5209b019d40c961d079e");
    CHECK(uuid == "f4de2fc0-36bf-5209-b019-d40c961d079e");
}

TEST_CASE("Parse UUID without dashes with curly braces", "[parseUuid]")
{
    const auto uuid = parseUuid("{f4de2fc036bf5209b019d40c961d079e}");
    CHECK(uuid == "f4de2fc0-36bf-5209-b019-d40c961d079e");
}

TEST_CASE("Parse incorrect UUID returns empty string", "[parseUuid]")
{
    const auto uuid = parseUuid("f4de2fc0");
    CHECK(uuid.empty());
}

// Creating test case for gameHistoryToCsv
TEST_CASE("Game history to csv", "[gameHistoryToCsv]")
{
    GameHistory history;

    GameData data;
    data.isGameActive = true;
    data.ball = 1;
    data.activePlayer = 1;
    data.players.insert(std::make_pair(1, PlayerState {1, 100}));
    data.timestamp = std::chrono::system_clock::time_point(10s);
    history.push_back(data);

    data.players.at(1).setScore(200, 0);
    data.timestamp = std::chrono::system_clock::time_point(15s);
    history.push_back(data);

    data.isGameActive = false;
    data.ball = 3;
    data.activePlayer = 2;
    data.players.insert(std::make_pair(2, PlayerState {2, 1000}));
    data.timestamp = std::chrono::system_clock::time_point(20s);
    data.modes.addMode("MB:Multiball");
    data.modes.addMode("MB:Multiball2");
    history.push_back(data);

    // Completed modes are events, they are reported in a single row only
    data.timestamp = std::chrono::system_clock::time_point(25s);
    data.completedModes.addMode("MB:Multiball");
    data.completedModes.addMode("NA:SomeMode");
    history.push_back(data);

    std::string csv = gameHistoryToCsv(history);
    std::string expectedCsv =
            "time,p1,p2,p3,p4,p5,p6,player,ball,game_modes,completed_modes\n"
            "10,100,,,,,,1,1,,\n"
            "15,200,,,,,,1,1,,\n"
            "20,200,1000,,,,,2,3,\"MB:Multiball;MB:Multiball2\",\n"
            "25,200,1000,,,,,2,3,\"MB:Multiball;MB:Multiball2\",\"MB:Multiball;NA:SomeMode\"\n";
    CHECK(csv == expectedCsv);
}

TEST_CASE("parseActionGetUrl, happy path")
{
    constexpr auto url =
            "https://staging.scorbit.io/api/v2/scorbitrons/7a16ea98-48e8-4b2e-a1eb-cf282e3b81cc/"
            "sessions/da9e568d-ce3b-4493-9d5c-10cfe47a96de/";

    const auto sessionUuid = parseUrlUuid(url, "sessions");
    CHECK(sessionUuid == "da9e568d-ce3b-4493-9d5c-10cfe47a96de");

    const auto scorbitronUuid = parseUrlUuid(url, "scorbitrons");
    CHECK(scorbitronUuid == "7a16ea98-48e8-4b2e-a1eb-cf282e3b81cc");
}

TEST_CASE("parseActionGetUrl, with trailing slash")
{
    const auto sessionUuid = parseUrlUuid(
            "https://staging.scorbit.io/api/v2/sessions/74657788-455e-4dce-a4d4-38e6e5b765ad///",
            "sessions");
    CHECK(sessionUuid == "74657788-455e-4dce-a4d4-38e6e5b765ad");
}

TEST_CASE("isHostMatching compares hosts only")
{
    CHECK(isHostMatching("https://api.example.com:443/path/to.tgz",
                         "https://api.example.com:8080/other"));
    CHECK_FALSE(isHostMatching("https://api.example.com/foo", "https://cdn.other.net/bar"));
}

TEST_CASE("isInternalDownloadForAuth is host-based regardless of DeviceInfo::provider")
{
    // Updater and other integrations use non-"scorbitron" providers but still download
    // authenticated SDK artifacts from the configured API host. Gating internal auth on
    // provider alone breaks those flows.
    DeviceInfo nonScorbit;
    nonScorbit.provider = "integration_client";

    const std::string apiBase = "https://api.scorbit.io:443";
    CHECK(isInternalDownloadForAuth("https://api.scorbit.io/v2/sdk-1.0.0.tgz", apiBase,
                                    nonScorbit));

    DeviceInfo scorbitron;
    scorbitron.provider = "scorbitron";
    CHECK(isInternalDownloadForAuth("https://api.scorbit.io/v2/sdk-1.0.0.tgz", apiBase,
                                    scorbitron));

    CHECK_FALSE(isInternalDownloadForAuth("https://cdn.example.com/sdk-1.0.0.tgz", apiBase,
                                          nonScorbit));
}

TEST_CASE("leaderboardRequestTerminalError")
{
    CHECK(leaderboardRequestTerminalError(AuthStatus::AuthenticationFailed) == Error::AuthFailed);
    CHECK(leaderboardRequestTerminalError(AuthStatus::AuthenticatedUnpaired) == Error::NotPaired);
    CHECK_FALSE(leaderboardRequestTerminalError(AuthStatus::NotAuthenticated).has_value());
    CHECK_FALSE(leaderboardRequestTerminalError(AuthStatus::Authenticating).has_value());
    CHECK_FALSE(
            leaderboardRequestTerminalError(AuthStatus::AuthenticatedCheckingPairing).has_value());
    CHECK_FALSE(leaderboardRequestTerminalError(AuthStatus::AuthenticatedPaired).has_value());
}

TEST_CASE("isLeaderboardContextReady defers until paired context exists")
{
    const std::string machineUuid = "5f28c973-84e3-4779-8bfa-de9d6b264a2f";
    const std::optional<std::string> variantUuid = "ae1f422d-9b57-478c-ab45-aaa1bfe111e1";
    const std::optional<std::string> gameSlug = "cirqus-voltaire";

    CHECK_FALSE(isLeaderboardContextReady(AuthStatus::Authenticating, LeaderboardScope::Game,
                                          machineUuid, variantUuid, gameSlug));

    CHECK_FALSE(isLeaderboardContextReady(AuthStatus::AuthenticatedPaired, LeaderboardScope::Game,
                                          machineUuid, variantUuid, std::nullopt));

    CHECK(isLeaderboardContextReady(AuthStatus::AuthenticatedPaired, LeaderboardScope::Game,
                                    machineUuid, variantUuid, gameSlug));

    CHECK_FALSE(isLeaderboardContextReady(AuthStatus::AuthenticatedPaired,
                                          LeaderboardScope::Machine, "", variantUuid, gameSlug));

    CHECK(isLeaderboardContextReady(AuthStatus::AuthenticatedPaired, LeaderboardScope::Machine,
                                    machineUuid, variantUuid, gameSlug));

    CHECK_FALSE(isLeaderboardContextReady(AuthStatus::AuthenticatedPaired,
                                          LeaderboardScope::Variant, machineUuid, std::nullopt,
                                          gameSlug));

    CHECK(isLeaderboardContextReady(AuthStatus::AuthenticatedPaired, LeaderboardScope::Variant,
                                    machineUuid, variantUuid, gameSlug));
}

TEST_CASE("Only unsettled statuses are worth waiting for", "[authGate]")
{
    const std::vector<AuthStatus> needsPaired {AuthStatus::AuthenticatedPaired};

    CHECK(authGate(AuthStatus::AuthenticatedPaired, needsPaired, false) == AuthGate::Ready);

    // Authentication is still working; the status may yet become allowed.
    CHECK(authGate(AuthStatus::NotAuthenticated, needsPaired, false) == AuthGate::Pending);
    CHECK(authGate(AuthStatus::Authenticating, needsPaired, false) == AuthGate::Pending);
    CHECK(authGate(AuthStatus::AuthenticatedCheckingPairing, needsPaired, false)
          == AuthGate::Pending);

    // Settled elsewhere. Waiting here is what used to hang a request forever.
    CHECK(authGate(AuthStatus::AuthenticationFailed, needsPaired, false) == AuthGate::Terminal);
    CHECK(authGate(AuthStatus::AuthenticatedUnpaired, needsPaired, false) == AuthGate::Terminal);
}

TEST_CASE("Shutting down never waits", "[authGate]")
{
    const std::vector<AuthStatus> needsPaired {AuthStatus::AuthenticatedPaired};

    for (const auto status :
         {AuthStatus::NotAuthenticated, AuthStatus::Authenticating,
          AuthStatus::AuthenticatedCheckingPairing, AuthStatus::AuthenticationFailed,
          AuthStatus::AuthenticatedUnpaired}) {
        CHECK(authGate(status, needsPaired, true) == AuthGate::Terminal);
    }

    // An already-allowed request still runs; it does not need to wait for anything.
    CHECK(authGate(AuthStatus::AuthenticatedPaired, needsPaired, true) == AuthGate::Ready);
}

TEST_CASE("A request allowed while checking pairing runs immediately", "[authGate]")
{
    // The scorbitron PATCH is the one request permitted in this state, and the only thing that
    // can move the status on. It must never be parked behind the gate it exists to open.
    const std::vector<AuthStatus> allowed {AuthStatus::AuthenticatedCheckingPairing,
                                           AuthStatus::AuthenticatedPaired,
                                           AuthStatus::AuthenticatedUnpaired};

    CHECK(authGate(AuthStatus::AuthenticatedCheckingPairing, allowed, false) == AuthGate::Ready);
    CHECK(authGate(AuthStatus::AuthenticatedUnpaired, allowed, false) == AuthGate::Ready);
    CHECK(authGate(AuthStatus::Authenticating, allowed, false) == AuthGate::Pending);
    CHECK(authGate(AuthStatus::AuthenticationFailed, allowed, false) == AuthGate::Terminal);
}

TEST_CASE("Refresh lands before the client asks", "[centrifugoTokenRefreshDelay]")
{
    // The whole point of the cache: our refresh must complete before the Centrifugo client's
    // synchronous token callback runs, otherwise it finds a stale token and has to fetch one
    // itself, on its own strand.
    for (const std::chrono::seconds expiresIn : {5min, 10min, 30min, 60min, 1440min}) {
        const auto delay = centrifugoTokenRefreshDelay(expiresIn);
        const auto clientAsksAt = expiresIn - CF_CLIENT_REFRESH_BEFORE_EXPIRY;
        CHECK(delay < clientAsksAt);
        CHECK(delay >= CF_TOKEN_REFRESH_MIN_DELAY);
    }
}

TEST_CASE("Short-lived tokens do not spin the timer", "[centrifugoTokenRefreshDelay]")
{
    // A token that expires sooner than the client's own refresh window leaves no room to stay
    // ahead of it; the delay must still be bounded away from zero so the timer can't busy-loop.
    CHECK(centrifugoTokenRefreshDelay(0s) == CF_TOKEN_REFRESH_MIN_DELAY);
    CHECK(centrifugoTokenRefreshDelay(30s) == CF_TOKEN_REFRESH_MIN_DELAY);
    CHECK(centrifugoTokenRefreshDelay(3min) == CF_TOKEN_REFRESH_MIN_DELAY);

    // Negative can arrive from an already-expired token.
    CHECK(centrifugoTokenRefreshDelay(-1h) == CF_TOKEN_REFRESH_MIN_DELAY);
}

TEST_CASE("A probe without a deadline never expires", "[diagProbeDeadline]")
{
    // deadline_seconds is optional on the wire. An API build that does not send it, or sends 0,
    // must leave the probe working rather than be treated as "already expired" -- the device
    // ships ahead of the server and has to tolerate both.
    const auto now = std::chrono::steady_clock::now();

    CHECK_FALSE(diagProbeDeadline(0, now).has_value());
    CHECK_FALSE(diagProbeDeadline(-1, now).has_value());

    // And an absent deadline is never passed, however far the clock has moved.
    CHECK_FALSE(diagProbeDeadlinePassed(std::nullopt, now + 24h));
}

TEST_CASE("The budget is measured from receipt", "[diagProbeDeadline]")
{
    // The payload carries a duration, not an instant, so the deadline is anchored at the moment
    // the probe arrived. Anchoring it anywhere later would restart the clock after exactly the
    // queue delay this is meant to catch.
    const auto receivedAt = std::chrono::steady_clock::now();
    const auto deadline = diagProbeDeadline(15, receivedAt);

    REQUIRE(deadline.has_value());
    CHECK(*deadline == receivedAt + 15s);
}

TEST_CASE("A deadline passes only once it is reached", "[diagProbeDeadline]")
{
    const auto receivedAt = std::chrono::steady_clock::now();
    const auto deadline = diagProbeDeadline(15, receivedAt);

    CHECK_FALSE(diagProbeDeadlinePassed(deadline, receivedAt));
    CHECK_FALSE(diagProbeDeadlinePassed(deadline, receivedAt + 14s));

    // Exactly at the deadline counts as passed: the budget is spent.
    CHECK(diagProbeDeadlinePassed(deadline, receivedAt + 15s));
    CHECK(diagProbeDeadlinePassed(deadline, receivedAt + 1h));
}

TEST_CASE("The API's documented deadline range round-trips", "[diagProbeDeadline]")
{
    // machine.py publishes deadline_seconds in 1..60. Nothing in that range should collapse to
    // "no deadline", which is the failure that would silently restore today's behaviour.
    const auto receivedAt = std::chrono::steady_clock::now();
    for (const int seconds : {1, 15, 59, 60}) {
        const auto deadline = diagProbeDeadline(seconds, receivedAt);
        REQUIRE(deadline.has_value());
        CHECK_FALSE(diagProbeDeadlinePassed(deadline, receivedAt));
        CHECK(diagProbeDeadlinePassed(deadline, receivedAt + std::chrono::seconds {seconds}));
    }
}

// --- capture ingest: which statuses end the run ----------------------------

TEST_CASE("A 404 or 410 on capture ingest ends the run; nothing else does", "[wifiIngest]")
{
    // 410: the server closed the run. 404: the server has never heard of it -- the case SB-4938
    // found running for 3h45m, because only 410 was terminal and 404 fell through to "try again".
    CHECK(wifiIngestStatusEndsRun(410));
    CHECK(wifiIngestStatusEndsRun(404));

    // Transient shapes: a transport failure carries no status at all, and a server error or a
    // rejected body should not end a run that is otherwise still live.
    CHECK_FALSE(wifiIngestStatusEndsRun(0));
    CHECK_FALSE(wifiIngestStatusEndsRun(202));
    CHECK_FALSE(wifiIngestStatusEndsRun(400));
    CHECK_FALSE(wifiIngestStatusEndsRun(413));
    CHECK_FALSE(wifiIngestStatusEndsRun(500));
    CHECK_FALSE(wifiIngestStatusEndsRun(503));
}


// --- wifi capture sample payload -------------------------------------------
//
// These pin the SHAPE, not the plumbing. The server's WifiCaptureSampleIngestSerializer silently
// drops unknown keys, so a typo is a 202 plus a NULL column and no error anywhere. That is
// precisely how noise_dbm -- this ticket's own acceptance canary -- went unsent for four months
// behind nine passing tests, all of which tested output parsing rather than what gets POSTed.

namespace {

wifi::Sample makeFullSample()
{
    wifi::Sample sample;
    sample.link.kind = wifi::InterfaceKind::Wifi;
    sample.link.ssid = "venue-ap";
    sample.link.bssid = "00:11:22:33:44:55";
    sample.link.rssiDbm = -52;
    sample.link.noiseDbm = -95;
    sample.link.linkRateMbps = 144;
    sample.link.txRetryPct = 3.5;
    sample.link.beaconLossCount = 2;
    sample.link.freqMhz = 2437;
    sample.link.channel = 6;
    sample.gateway = wifi::ProbeResult {"192.168.1.1", 3, 0.0};
    sample.scorbit = wifi::ProbeResult {"sws.scorbit.io", 41, 10.0};
    sample.publicInternet = wifi::ProbeResult {"1.1.1.1", 22, 0.0};
    return sample;
}

} // namespace

TEST_CASE("A full wifi sample POSTs exactly the keys the server declares",
          "[buildWifiSamplePayload]")
{
    const auto j = buildWifiSamplePayload(makeFullSample());

    // Every key here is spelled to match WifiCaptureSampleIngestSerializer. If one is renamed
    // server-side this test is the thing that fails, rather than a venue's data quietly going
    // NULL.
    const std::set<std::string> expected {
            "ts",          "source",
            "is_final",    "rssi_dbm",         "noise_dbm",   "link_rate_mbps",
            "tx_retry_pct", "beacon_loss_count", "freq_mhz",   "channel",
            "gateway_rtt_ms", "gateway_loss_pct", "scorbit_rtt_ms", "scorbit_loss_pct",
            "public_rtt_ms", "public_loss_pct"};

    std::set<std::string> actual;
    for (const auto &item : j.items()) {
        actual.insert(item.key());
    }

    CHECK(actual == expected);
}

TEST_CASE("A sample never carries the SSID or BSSID", "[buildWifiSamplePayload]")
{
    // SPEC-0007 "Identifier-Free Capture" (SB-4984): the link knows both, the body must not.
    auto sample = makeFullSample();
    sample.link.ssid = "VenueWiFi";
    sample.link.bssid = "aa:bb:cc:dd:ee:ff";

    const auto body = buildWifiSamplePayload(sample).dump();

    CHECK(body.find("VenueWiFi") == std::string::npos);
    CHECK(body.find("aa:bb:cc:dd:ee:ff") == std::string::npos);
}

TEST_CASE("noise_dbm is sent, so SNR is derivable", "[buildWifiSamplePayload]")
{
    // The acceptance canary for SB-3461: a sample must carry BOTH rssi_dbm and noise_dbm.
    const auto j = buildWifiSamplePayload(makeFullSample());

    REQUIRE(j.contains("rssi_dbm"));
    REQUIRE(j.contains("noise_dbm"));
    CHECK(j["rssi_dbm"].get<int>() == -52);
    CHECK(j["noise_dbm"].get<int>() == -95);
}

TEST_CASE("source distinguishes ethernet from wifi", "[buildWifiSamplePayload]")
{
    // Without this field every sample is recorded as "wifi" server-side, which is what blocks
    // SB-3465 -- an Ethernet sampler that cannot say so produces mislabelled rows.
    auto sample = makeFullSample();
    CHECK(buildWifiSamplePayload(sample)["source"].get<std::string>() == "wifi");

    sample.link.kind = wifi::InterfaceKind::Ethernet;
    CHECK(buildWifiSamplePayload(sample)["source"].get<std::string>() == "ethernet");

    // Unknown must not invent a third value: the server's ChoiceField would 400 it.
    sample.link.kind = wifi::InterfaceKind::Unknown;
    CHECK(buildWifiSamplePayload(sample)["source"].get<std::string>() == "wifi");
}

TEST_CASE("Unmeasured metrics are omitted, not sent as null", "[buildWifiSamplePayload]")
{
    // A sampler round that measured nothing -- e.g. `iw` missing, which yields empty stdout and
    // parses as absent across the board -- must not fabricate keys.
    wifi::Sample sample;
    sample.link.kind = wifi::InterfaceKind::Wifi;

    const auto j = buildWifiSamplePayload(sample);

    CHECK_FALSE(j.contains("rssi_dbm"));
    CHECK_FALSE(j.contains("noise_dbm"));
    CHECK_FALSE(j.contains("gateway_rtt_ms"));
    CHECK_FALSE(j.contains("public_loss_pct"));

    // The always-present keys stay present.
    CHECK(j.contains("ts"));
    CHECK(j.contains("source"));
    CHECK(j.contains("is_final"));
}

TEST_CASE("dependency_checks is serialised as a nested object", "[buildWifiSamplePayload]")
{
    // The exact-key test above uses a sample with no dependency checks, so it would pass whether
    // this branch worked or was misspelled. This is the case that actually covers it.
    auto sample = makeFullSample();
    sample.dependencyChecks = {
            {"link", "ok"},   {"dhcp_gateway", "ok"},         {"dns", "ok"},
            {"clock", "ok"},  {"rest443", "intermittent"},    {"wss443", "blocked"},
    };

    const auto j = buildWifiSamplePayload(sample);

    REQUIRE(j.contains("dependency_checks"));
    REQUIRE(j["dependency_checks"].is_object());
    CHECK(j["dependency_checks"].size() == 6);
    CHECK(j["dependency_checks"]["wss443"].get<std::string>() == "blocked");
    CHECK(j["dependency_checks"]["rest443"].get<std::string>() == "intermittent");
}

TEST_CASE("An empty dependency map omits the key entirely", "[buildWifiSamplePayload]")
{
    // The serializer defaults it to {} and the panel renders every missing key as "unknown", so
    // an absent dict and an empty one mean the same thing -- send the shorter one.
    auto sample = makeFullSample();
    sample.dependencyChecks.clear();

    CHECK_FALSE(buildWifiSamplePayload(sample).contains("dependency_checks"));
}
