/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 */

#include <diagnostics/wifi/wifi_diagnostics.h>
#include <diagnostics/wifi/network_monitor.h>
#include <atomic>
#include <filesystem>
#include <memory>
#include <random>
#include <thread>
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
    // Set by whichever parser produced the info. buildDependencyChecks() treats an empty backend
    // as "nothing measured this" and omits the link key, so a fixture without it is not a
    // connected link -- it is an absent one.
    link.backend = "iw";
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

TEST_CASE("An unmeasured link omits the key rather than reporting blocked", "[wifi][dependency]")
{
    // collectLinkInfo() returning nothing leaves a default LinkInfo: connected == false, backend
    // empty. Reporting that as "blocked" would invent a link failure on any platform without a
    // collector -- the panel's "unknown" is the truthful rendering.
    LinkInfo unmeasured;

    const auto checks = buildDependencyChecks(unmeasured, std::nullopt, {}, std::nullopt);
    CHECK(checks.find("link") == checks.end());
    CHECK(checks.empty());
}

TEST_CASE("A measured link that is down still reports blocked", "[wifi][dependency]")
{
    // The flip side: evidence of a down link is not the same as absence of evidence.
    LinkInfo down;
    down.kind = InterfaceKind::Wifi;
    down.backend = "iw";
    down.connected = false;

    CHECK(buildDependencyChecks(down, std::nullopt, {}, std::nullopt).at("link") == "blocked");
}

// --- 410 is terminal for a run ---------------------------------------------

namespace {

/// A state-file path unique to this call.
///
/// NetworkMonitor writes a real file, and a fixed name is not safe here: the suite runs under
/// `ctest -j8`, and several worktrees of this repo share one system temp directory, so two runs
/// could delete or overwrite each other's state and fail for reasons that have nothing to do with
/// what is under test. That is exactly the kind of flakiness SB-4875 already tracks.
std::string uniqueStateFilePath()
{
    static std::atomic_uint64_t counter {0};
    std::random_device rd;
    const auto name = "scorbit_capture_test_" + std::to_string(rd()) + "_"
                    + std::to_string(counter.fetch_add(1)) + ".state";
    return (std::filesystem::temp_directory_path() / name).string();
}

} // namespace

TEST_CASE("A run the server has closed retires without a final sample", "[wifi][runclosed]")
{
    // SPEC-0007 makes 410 terminal: the server has ended this run, so anything further the device
    // posts lands in another 410. Before this, the sampler had no way to learn that and kept
    // posting for the rest of the requested duration.
    //
    // The flag is set before start() so the assertion is deterministic -- the retirement check is
    // the first thing the loop does, which also keeps this test off the network (the DNS probe
    // would otherwise be due immediately on the first iteration).
    auto runClosed = std::make_shared<std::atomic_bool>(true);

    NetworkMonitor::Options options;
    options.runId = "run-closed-test";
    options.requestedDuration = std::chrono::seconds {300};
    options.runClosed = runClosed;
    const auto stateFilePath = uniqueStateFilePath();
    options.stateFilePath = stateFilePath;
    options.commandRunner = [](const std::string &, const std::vector<std::string> &) {
        return CommandResult {0, ""};
    };

    std::atomic_int samples {0};
    NetworkMonitor::Callbacks callbacks;
    callbacks.onSample = [&samples](const Sample &) { ++samples; };

    NetworkMonitor monitor {std::move(options), std::move(callbacks)};
    REQUIRE(monitor.start());

    // Wait for the condition rather than sleeping a fixed span: this repo already has timing
    // flakiness under -j8 (SB-4875) and a fixed sleep would add to it.
    for (int i = 0; i < 400 && monitor.isActive(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds {5});
    }

    CHECK_FALSE(monitor.isActive());

    // Joins. The reason guard means this does not relabel the retirement.
    monitor.stop("shutdown");

    CHECK(monitor.endReason() == "run_closed");
    // The point of the whole exercise: nothing was posted into the closed run, not even the final
    // sample that a normal stop emits.
    CHECK(samples.load() == 0);

    // run() removes the state file itself, so this only matters when an assertion above failed --
    // but a failing test should not also leave litter behind for the next one to trip over.
    std::error_code ec;
    std::filesystem::remove(stateFilePath, ec);
}

TEST_CASE("A run closed mid-capture emits no final sample", "[wifi][runclosed]")
{
    // The case the flag-set-before-start test does not reach: the run is still live when sampling
    // begins and is closed while it is under way, which is the realistic shape -- a 410 usually
    // arrives *because* the run is ending.
    //
    // Deterministic without sleeping on a race: the flag is set from inside onSample, so the
    // ordering is fixed by the callback rather than by timing. The sampler takes its first sample,
    // the run is closed during that callback, and the loop must then retire WITHOUT the final
    // sample a normal stop would emit.
    auto runClosed = std::make_shared<std::atomic_bool>(false);

    NetworkMonitor::Options options;
    options.runId = "closed-mid-capture";
    options.requestedDuration = std::chrono::seconds {300};
    options.runClosed = runClosed;
    const auto stateFilePath = uniqueStateFilePath();
    options.stateFilePath = stateFilePath;
    options.commandRunner = [](const std::string &, const std::vector<std::string> &) {
        return CommandResult {0, ""};
    };
    // Empty target short-circuits resolveHost(), keeping this unit test off the network -- the
    // dependency probe is otherwise due on the first iteration.
    options.scorbitProbeTarget = "";

    std::atomic_int ordinary {0};
    std::atomic_int finals {0};

    NetworkMonitor::Callbacks callbacks;
    callbacks.onSample = [&](const Sample &sample) {
        if (sample.isFinal) {
            ++finals;
            return;
        }
        ++ordinary;
        // The server closes the run while the capture is live.
        runClosed->store(true, std::memory_order_release);
    };

    NetworkMonitor monitor {std::move(options), std::move(callbacks)};
    REQUIRE(monitor.start());

    for (int i = 0; i < 400 && monitor.isActive(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds {5});
    }
    CHECK_FALSE(monitor.isActive());
    monitor.stop("shutdown");

    CHECK(monitor.endReason() == "run_closed");
    CHECK(ordinary.load() >= 1);
    // The assertion this test exists for. Before the final-check re-read, the loop exited with a
    // stale local and posted one last sample into a run the server had already closed.
    CHECK(finals.load() == 0);

    std::error_code ec;
    std::filesystem::remove(stateFilePath, ec);
}

TEST_CASE("A run closed as it is stopped posts no final sample", "[wifi][runclosed]")
{
    // THIS is the interleaving the final-check re-read exists for, and the one the two tests above
    // cannot reach. Both of those exit the loop through the closed-run branch, which the old code
    // also handled -- they describe the behaviour rather than pin the fix.
    //
    // Here the loop exits WITHOUT consulting the flag at all: stop() clears m_active while the
    // sampler is parked in its condition-variable wait, so `while (m_active)` fails and the check
    // at the top of the loop never runs again. Whether a final sample is posted into the closed
    // run then depends entirely on re-reading the flag at the gate. The old cached local was false
    // on this path, so it posted one.
    auto runClosed = std::make_shared<std::atomic_bool>(false);

    NetworkMonitor::Options options;
    options.runId = "closed-at-stop";
    options.requestedDuration = std::chrono::seconds {300};
    options.runClosed = runClosed;
    const auto stateFilePath = uniqueStateFilePath();
    options.stateFilePath = stateFilePath;
    options.commandRunner = [](const std::string &, const std::vector<std::string> &) {
        return CommandResult {0, ""};
    };
    options.scorbitProbeTarget = "";

    std::atomic_int ordinary {0};
    std::atomic_int finals {0};

    NetworkMonitor::Callbacks callbacks;
    callbacks.onSample = [&](const Sample &sample) {
        if (sample.isFinal) {
            ++finals;
        } else {
            ++ordinary;
        }
    };

    NetworkMonitor monitor {std::move(options), std::move(callbacks)};
    REQUIRE(monitor.start());

    // Wait for the first sample, after which the sampler parks in its 1s wait.
    for (int i = 0; i < 400 && ordinary.load() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds {5});
    }
    REQUIRE(ordinary.load() >= 1);

    // A margin, not a race: the sampler reaches the wait within microseconds of the callback
    // returning. If it has not, the loop takes the closed-run branch instead and the assertion
    // below still holds -- so this can only be less informative, never falsely red.
    std::this_thread::sleep_for(std::chrono::milliseconds {50});

    runClosed->store(true, std::memory_order_release);
    monitor.stop("manual_stop");

    CHECK(finals.load() == 0);

    std::error_code ec;
    std::filesystem::remove(stateFilePath, ec);
}
