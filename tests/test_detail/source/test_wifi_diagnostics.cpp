/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 */

#include <diagnostics/wifi/wifi_diagnostics.h>
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>

using namespace scorbit::detail::wifi;

TEST_CASE("Wi-Fi channel conversion handles common bands", "[wifi]")
{
    CHECK(wifiChannelFromFrequency(2412) == 1);
    CHECK(wifiChannelFromFrequency(2437) == 6);
    CHECK(wifiChannelFromFrequency(2484) == 14);
    CHECK(wifiChannelFromFrequency(5180) == 36);
    CHECK(wifiChannelFromFrequency(5955) == 1);
    CHECK(wifiChannelFromFrequency(1000) == 0);
}

TEST_CASE("iw link output is parsed", "[wifi]")
{
    const auto parsed = parseIwLink(
            R"(Connected to aa:bb:cc:dd:ee:ff (on wlan0)
	SSID: ScorbitVenue
	freq: 2437
	signal: -58 dBm
	tx bitrate: 72.2 MBit/s)",
            "wlan0");

    REQUIRE(parsed);
    CHECK(parsed->connected);
    CHECK(parsed->backend == "iw");
    CHECK(parsed->interfaceName == "wlan0");
    CHECK(parsed->ssid == "ScorbitVenue");
    CHECK(parsed->bssid == "aa:bb:cc:dd:ee:ff");
    CHECK(parsed->freqMhz == 2437);
    CHECK(parsed->channel == 6);
    CHECK(parsed->rssiDbm == -58);
    CHECK(parsed->linkRateMbps == 72);
}

TEST_CASE("Linux station dump retry metrics are parsed", "[wifi]")
{
    LinkInfo base;
    const auto parsed = parseIwStationDump(
            R"(Station aa:bb:cc:dd:ee:ff (on wlan0)
	tx packets:	90
	tx retries:	10
	beacon loss:	2)",
            base);

    REQUIRE(parsed);
    REQUIRE(parsed->txRetryPct);
    CHECK(*parsed->txRetryPct == 10.0);
    CHECK(parsed->beaconLossCount == 2);
}

TEST_CASE("netsh wlan output is parsed", "[wifi]")
{
    const auto parsed = parseNetshWlanInterfaces(
            R"(There is 1 interface on the system:

    Name                   : Wi-Fi
    State                  : connected
    SSID                   : VenueWifi
    BSSID                  : 00:11:22:33:44:55
    Signal                 : 76%
    Channel                : 11
    Receive rate (Mbps)    : 144.4
    Transmit rate (Mbps)   : 86.7)");

    REQUIRE(parsed);
    CHECK(parsed->connected);
    CHECK(parsed->backend == "netsh");
    CHECK(parsed->interfaceName == "Wi-Fi");
    CHECK(parsed->ssid == "VenueWifi");
    CHECK(parsed->bssid == "00:11:22:33:44:55");
    CHECK(parsed->rssiDbm == -62);
    CHECK(parsed->channel == 11);
    CHECK(parsed->linkRateMbps == 144);
}

TEST_CASE("macOS airport output is parsed", "[wifi]")
{
    const auto parsed = parseAirportInfo(
            R"(     agrCtlRSSI: -64
          state: running
        lastTxRate: 156
            SSID: VenueWifi
           BSSID: a1:b2:c3:d4:e5:f6
         channel: 149,80)");

    REQUIRE(parsed);
    CHECK(parsed->connected);
    CHECK(parsed->backend == "airport");
    CHECK(parsed->ssid == "VenueWifi");
    CHECK(parsed->bssid == "a1:b2:c3:d4:e5:f6");
    CHECK(parsed->rssiDbm == -64);
    CHECK(parsed->linkRateMbps == 156);
    CHECK(parsed->channel == 149);
}

TEST_CASE("macOS networksetup output is parsed", "[wifi]")
{
    const auto parsed = parseNetworksetupAirportNetwork("Current Wi-Fi Network: VenueWifi", "en0");

    REQUIRE(parsed);
    CHECK(parsed->connected);
    CHECK(parsed->backend == "networksetup");
    CHECK(parsed->interfaceName == "en0");
    CHECK(parsed->ssid == "VenueWifi");
}

TEST_CASE("macOS ipconfig getsummary output is parsed", "[wifi]")
{
    const auto parsed = parseIpconfigGetsummary(
            R"(<dictionary> {
  BSSID : 11:22:33:44:55:66
  InterfaceType : WiFi
  LinkStatusActive : TRUE
  SSID : VenueWifi
})",
            "en0");

    REQUIRE(parsed);
    CHECK(parsed->connected);
    CHECK(parsed->backend == "ipconfig");
    CHECK(parsed->interfaceName == "en0");
    CHECK(parsed->ssid == "VenueWifi");
    CHECK(parsed->bssid == "11:22:33:44:55:66");
}

TEST_CASE("ping output is parsed across platforms", "[wifi]")
{
    const auto linuxPing = parsePingOutput(
            "3 packets transmitted, 3 received, 0% packet loss, time 2002ms\n"
            "rtt min/avg/max/mdev = 10.123/12.500/15.000/1.000 ms",
            "1.1.1.1");
    REQUIRE(linuxPing);
    CHECK(linuxPing->lossPct == 0.0);
    CHECK(linuxPing->rttMs == 13);

    const auto windowsPing = parsePingOutput(
            "Packets: Sent = 3, Received = 2, Lost = 1 (33% loss),\n"
            "Minimum = 10ms, Maximum = 20ms, Average = 15ms",
            "1.1.1.1");
    REQUIRE(windowsPing);
    CHECK(windowsPing->lossPct == 33.0);
    CHECK(windowsPing->rttMs == 15);
}

TEST_CASE("default gateways are parsed", "[wifi]")
{
    CHECK(parseDefaultGateway("default via 192.168.1.1 dev wlan0 proto dhcp")
          == "192.168.1.1");
    CHECK(parseDefaultGateway("gateway: 10.0.0.1\ninterface: en0") == "10.0.0.1");
    CHECK(parseDefaultGateway("0.0.0.0          0.0.0.0      172.16.0.1    172.16.0.5")
          == "172.16.0.1");
}

#ifndef _WIN32
TEST_CASE("runCommand reports a child's real exit code", "[wifi]")
{
    // POSIX pclose() yields a WAIT STATUS, not an exit code, so a child exiting 1
    // used to surface as 256. Nothing in wifi_diagnostics.cpp noticed: every test
    // there is `exitCode == 0`, and a clean exit is status 0 under either reading.
    // What was wrong was the value itself, and network_monitor.cpp reports it
    // verbatim as the scan event's `exit_code`, so a failed `iw scan` told the
    // server 256.
    //
    // POSIX-only: the shell invocation has no cmd.exe equivalent, and _pclose
    // already returns the code directly on Windows.
    CHECK(runCommand("sh", {"-c", "exit 0"}).exitCode == 0);
    CHECK(runCommand("sh", {"-c", "exit 1"}).exitCode == 1);
    CHECK(runCommand("sh", {"-c", "exit 7"}).exitCode == 7);
}
#endif


// --- dependency_checks -----------------------------------------------------
//
// Both the keys and the statuses are unvalidated end to end: the server stores the dict as a bare
// DictField and the panel does `checks[key] || "unknown"`. So a typo in either renders as
// "unknown" with no error anywhere. These tests are the only enforcement that exists.

namespace {

LinkInfo connectedLink()
{
    LinkInfo link;
    link.kind = InterfaceKind::Wifi;
    link.connected = true;
    return link;
}

ProbeResult gatewayWithLoss(double lossPct)
{
    ProbeResult probe;
    probe.target = "192.168.1.1";
    probe.rttMs = 3;
    probe.lossPct = lossPct;
    return probe;
}

} // namespace

TEST_CASE("dependency_checks uses only the six keys the panel renders", "[wifi][dependency]")
{
    DependencySnapshot snapshot;
    snapshot.realtimeConnected = true;
    snapshot.sinceLastRestSuccess = std::chrono::seconds {10};
    snapshot.clockDelta = std::chrono::seconds {1};

    const auto checks =
            buildDependencyChecks(connectedLink(), gatewayWithLoss(0.0), snapshot, true);

    // Exactly NetworkDiagnosticsLive.vue's DEP_KEYS. Note tls443 is deliberately absent: the spec
    // names it, nothing renders it, and producing it would cost an active TLS handshake.
    const std::set<std::string> expected {"link",  "dhcp_gateway", "dns",
                                          "clock", "rest443",      "wss443"};
    std::set<std::string> actual;
    for (const auto &[key, value] : checks) {
        actual.insert(key);
    }
    CHECK(actual == expected);

    // And every value is one the panel knows how to colour.
    const std::set<std::string> legal {"ok", "blocked", "intermittent"};
    for (const auto &[key, value] : checks) {
        INFO("key=" << key << " value=" << value);
        CHECK(legal.count(value) == 1);
    }
}

TEST_CASE("A key with no evidence is omitted rather than guessed", "[wifi][dependency]")
{
    // An owner that supplies no provider, on a round where the gateway was not probed and DNS has
    // not run yet. Only `link` is knowable, so only `link` is reported -- the rest render as
    // "unknown", which is the honest answer. Emitting "ok" here would be the -256 mistake again.
    const auto checks =
            buildDependencyChecks(connectedLink(), std::nullopt, {}, std::nullopt);

    CHECK(checks.size() == 1);
    CHECK(checks.at("link") == "ok");
}

TEST_CASE("Gateway loss grades into the three statuses", "[wifi][dependency]")
{
    const auto statusFor = [](double loss) {
        return buildDependencyChecks(connectedLink(), gatewayWithLoss(loss), {}, std::nullopt)
                .at("dhcp_gateway");
    };

    CHECK(statusFor(0.0) == "ok");
    CHECK(statusFor(40.0) == "intermittent");
    CHECK(statusFor(100.0) == "blocked");
}

TEST_CASE("A disconnected link reports blocked", "[wifi][dependency]")
{
    LinkInfo link;
    link.kind = InterfaceKind::Wifi;
    link.connected = false;

    CHECK(buildDependencyChecks(link, std::nullopt, {}, std::nullopt).at("link") == "blocked");
}

TEST_CASE("rest443 grades quiet separately from broken", "[wifi][dependency]")
{
    // The SDK only calls the API when it has something to say, so a gap is not itself a failure.
    const auto statusFor = [](std::chrono::seconds age) {
        DependencySnapshot snapshot;
        snapshot.sinceLastRestSuccess = age;
        return buildDependencyChecks(connectedLink(), std::nullopt, snapshot, std::nullopt)
                .at("rest443");
    };

    CHECK(statusFor(std::chrono::seconds {5}) == "ok");
    CHECK(statusFor(std::chrono::minutes {5}) == "intermittent");
    CHECK(statusFor(std::chrono::hours {1}) == "blocked");
}

TEST_CASE("clock drift is judged on magnitude, either direction", "[wifi][dependency]")
{
    const auto statusFor = [](std::chrono::seconds delta) {
        DependencySnapshot snapshot;
        snapshot.clockDelta = delta;
        return buildDependencyChecks(connectedLink(), std::nullopt, snapshot, std::nullopt)
                .at("clock");
    };

    CHECK(statusFor(std::chrono::seconds {2}) == "ok");
    CHECK(statusFor(std::chrono::seconds {-2}) == "ok");
    // A device running an hour behind is as broken as one an hour ahead.
    CHECK(statusFor(std::chrono::hours {1}) == "blocked");
    CHECK(statusFor(-std::chrono::hours {1}) == "blocked");
}

TEST_CASE("wss443 and dns are straight booleans", "[wifi][dependency]")
{
    DependencySnapshot up;
    up.realtimeConnected = true;
    DependencySnapshot down;
    down.realtimeConnected = false;

    CHECK(buildDependencyChecks(connectedLink(), std::nullopt, up, std::nullopt).at("wss443")
          == "ok");
    CHECK(buildDependencyChecks(connectedLink(), std::nullopt, down, std::nullopt).at("wss443")
          == "blocked");

    CHECK(buildDependencyChecks(connectedLink(), std::nullopt, {}, true).at("dns") == "ok");
    CHECK(buildDependencyChecks(connectedLink(), std::nullopt, {}, false).at("dns")
          == "blocked");
}
