/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
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

    std::string csv = gameHistoryToCsv(history);
    std::string expectedCsv = "time,p1,p2,p3,p4,p5,p6,player,ball,game_modes\n"
                              "10,100,,,,,,1,1,\n"
                              "15,200,,,,,,1,1,\n"
                              "20,200,1000,,,,,2,3,\"MB:Multiball;MB:Multiball2\"\n";
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

    for (const auto status : {AuthStatus::NotAuthenticated, AuthStatus::Authenticating,
                              AuthStatus::AuthenticatedCheckingPairing,
                              AuthStatus::AuthenticationFailed,
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