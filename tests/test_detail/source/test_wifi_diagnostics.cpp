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
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
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

// --- lifecycle -------------------------------------------------------------
//
// Nothing tested NetworkMonitor's lifecycle until now, which is exactly why two separate
// std::terminate() bugs lived in it: stop() skipped the join whenever m_active was already false,
// and the sampler clears that flag itself on deadline expiry -- so the ORDINARY completion path
// destroyed a joinable std::thread. Each of these cases would have caught it.

namespace {

NetworkMonitor::Options lifecycleOptions(std::chrono::seconds duration)
{
    NetworkMonitor::Options options;
    options.runId = "lifecycle";
    options.requestedDuration = duration;
    options.stateFilePath = uniqueStateFilePath();
    options.commandRunner = [](const std::string &, const std::vector<std::string> &) {
        return CommandResult {0, ""};
    };
    // Keeps the active dependency probe off the network; it is otherwise due immediately.
    options.scorbitProbeTarget = "";
    return options;
}

} // namespace

TEST_CASE("A capture that reaches its deadline can be destroyed", "[wifi][lifecycle]")
{
    // THE regression case. The sampler clears m_active itself when the deadline passes, so by the
    // time ~NetworkMonitor() runs the flag is already false -- which is precisely the state the old
    // stop() used to skip the join on, destroying a joinable thread and aborting the process.
    auto options = lifecycleOptions(std::chrono::seconds {0});
    const auto stateFilePath = options.stateFilePath;

    {
        NetworkMonitor monitor {std::move(options), {}};
        REQUIRE(monitor.start());

        for (int i = 0; i < 400 && monitor.isActive(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds {5});
        }
        REQUIRE_FALSE(monitor.isActive());

        // The destructor runs here, on an already-finished sampler. Reaching the line after this
        // scope at all is the assertion.
        CHECK(monitor.endReason() == "expired");
    }

    SUCCEED("destroyed a monitor whose sampler had already exited");

    std::error_code ec;
    std::filesystem::remove(stateFilePath, ec);
}

TEST_CASE("A deadline expiry is not relabelled by the destructor", "[wifi][lifecycle]")
{
    // ~NetworkMonitor() calls stop("shutdown"). If that overwrote the reason unconditionally, every
    // naturally-expired run would report end_reason "shutdown" instead.
    auto options = lifecycleOptions(std::chrono::seconds {0});
    const auto stateFilePath = options.stateFilePath;

    NetworkMonitor monitor {std::move(options), {}};
    REQUIRE(monitor.start());
    for (int i = 0; i < 400 && monitor.isActive(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds {5});
    }

    monitor.stop("shutdown");
    CHECK(monitor.endReason() == "expired");

    std::error_code ec;
    std::filesystem::remove(stateFilePath, ec);
}

TEST_CASE("stop() is idempotent and safe before start()", "[wifi][lifecycle]")
{
    auto options = lifecycleOptions(std::chrono::seconds {300});
    const auto stateFilePath = options.stateFilePath;

    {
        // Never started: no thread was ever spawned, so the join must cope with that too.
        NetworkMonitor monitor {lifecycleOptions(std::chrono::seconds {300}), {}};
        monitor.stop("manual_stop");
        monitor.stop("manual_stop");
    }

    {
        NetworkMonitor monitor {std::move(options), {}};
        REQUIRE(monitor.start());
        monitor.stop("manual_stop");
        // Second stop finds a thread that is no longer joinable.
        monitor.stop("manual_stop");
        CHECK_FALSE(monitor.isActive());
        CHECK(monitor.endReason() == "manual_stop");
    }

    SUCCEED("double stop, and stop before start, both survived");

    std::error_code ec;
    std::filesystem::remove(stateFilePath, ec);
}

TEST_CASE("requestStop() returns without joining, and the destructor still joins",
          "[wifi][lifecycle]")
{
    // The split that lets the Centrifugo dispatcher end a capture without blocking: requestStop()
    // asks the sampler to finish, and whoever destroys the object later does the waiting.
    auto options = lifecycleOptions(std::chrono::seconds {300});
    const auto stateFilePath = options.stateFilePath;

    {
        NetworkMonitor monitor {std::move(options), {}};
        REQUIRE(monitor.start());

        monitor.requestStop("manual_stop");
        CHECK(monitor.endReason() == "manual_stop");

        // Destructor joins here. If requestStop() had left the thread unjoinable-but-running, or
        // had itself joined, this is where it would show.
    }

    SUCCEED("requestStop() then destroy completed cleanly");

    std::error_code ec;
    std::filesystem::remove(stateFilePath, ec);
}

// --- the event listener ends with the run ----------------------------------
//
// SB-4938. On a Scorbitron the passive listener is wpa_supplicant over D-Bus, and it was only ever
// stopped by stop() -- which nobody calls on a run that ends by itself. The sampler stopped on
// time; the listener kept turning wpa_supplicant's bgscan and re-association signals into
// scan/assoc POSTs against the finished run for 3h45m. The fake below is what makes that
// observable on a host without D-Bus.

namespace {

struct ListenerProbe {
    std::atomic_int starts {0};
    std::atomic_int stops {0};
    std::function<void(Event)> callback;
    // When set, stop() parks on the gate until release(): a stand-in for the
    // D-Bus listener's join taking a while, so a test can hold a teardown in
    // flight and watch what the other thread does meanwhile.
    bool blockStop {false};
    std::mutex gate;
    std::condition_variable gateCv;
    bool stopEntered {false};
    bool released {false};

    void release()
    {
        {
            std::scoped_lock lock(gate);
            released = true;
        }
        gateCv.notify_all();
    }
};

class FakeListener : public EventListener
{
public:
    FakeListener(std::shared_ptr<ListenerProbe> probe, std::function<void(Event)> callback)
        : m_probe(std::move(probe))
    {
        m_probe->callback = std::move(callback);
    }

    bool start() override
    {
        ++m_probe->starts;
        return true;
    }

    void stop() override
    {
        ++m_probe->stops;
        if (!m_probe->blockStop) {
            return;
        }
        std::unique_lock lock(m_probe->gate);
        m_probe->stopEntered = true;
        m_probe->gateCv.notify_all();
        m_probe->gateCv.wait(lock, [this] { return m_probe->released; });
    }

private:
    std::shared_ptr<ListenerProbe> m_probe;
};

NetworkMonitor::Options listenerOptions(std::chrono::seconds duration,
                                        std::shared_ptr<ListenerProbe> probe)
{
    auto options = lifecycleOptions(duration);
    options.eventListenerFactory = [probe](std::function<void(Event)> callback) {
        return std::make_unique<FakeListener>(probe, std::move(callback));
    };
    return options;
}

bool waitFor(const std::function<bool()> &condition)
{
    for (int i = 0; i < 400 && !condition(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds {5});
    }
    return condition();
}

} // namespace

TEST_CASE("The event listener is started with the run and wired to onEvent", "[wifi][listener]")
{
    auto probe = std::make_shared<ListenerProbe>();
    auto options = listenerOptions(std::chrono::seconds {300}, probe);
    const auto stateFilePath = options.stateFilePath;

    std::atomic_int events {0};
    NetworkMonitor::Callbacks callbacks;
    callbacks.onEvent = [&events](const Event &) { ++events; };

    NetworkMonitor monitor {std::move(options), std::move(callbacks)};
    REQUIRE(monitor.start());
    CHECK(probe->starts.load() == 1);

    // A listener event reaches the owner exactly as a D-Bus signal would.
    REQUIRE(probe->callback);
    Event event;
    event.kind = "scan";
    probe->callback(event);
    CHECK(events.load() == 1);

    monitor.stop("manual_stop");
    CHECK(probe->stops.load() == 1);

    std::error_code ec;
    std::filesystem::remove(stateFilePath, ec);
}

TEST_CASE("A run that expires on its own stops its event listener", "[wifi][listener]")
{
    // THE SB-4938 case. Nobody calls stop() on an expired run, so the assertion must hold before
    // stop() is ever called -- that is exactly the window the listener used to survive in.
    auto probe = std::make_shared<ListenerProbe>();
    auto options = listenerOptions(std::chrono::seconds {0}, probe);
    const auto stateFilePath = options.stateFilePath;

    NetworkMonitor monitor {std::move(options), {}};
    REQUIRE(monitor.start());

    // m_active clears a moment before the listener is stopped, so wait on the stop itself.
    CHECK(waitFor([&] { return probe->stops.load() == 1; }));
    CHECK_FALSE(monitor.isActive());
    CHECK(monitor.endReason() == "expired");

    // The eventual stop() finds nothing left to stop: no double teardown.
    monitor.stop("shutdown");
    CHECK(probe->stops.load() == 1);

    std::error_code ec;
    std::filesystem::remove(stateFilePath, ec);
}

TEST_CASE("The final sample waits for a listener teardown already in flight", "[wifi][listener]")
{
    // Copilot review on sdk#208. stop() and the sampler both tear the listener down; if stop()
    // wins the pointer and is still joining the listener, the sampler must not go on to emit
    // the final sample -- the listener's thread may still be inside its callback, and an event
    // would land after the sample that closes the run.
    auto probe = std::make_shared<ListenerProbe>();
    probe->blockStop = true;
    auto options = listenerOptions(std::chrono::seconds {300}, probe);
    const auto stateFilePath = options.stateFilePath;

    std::atomic_int finals {0};
    NetworkMonitor::Callbacks callbacks;
    callbacks.onSample = [&finals](const Sample &sample) {
        if (sample.isFinal) {
            ++finals;
        }
    };

    NetworkMonitor monitor {std::move(options), std::move(callbacks)};
    REQUIRE(monitor.start());

    // An external stop: it asks the sampler to finish, then parks inside the listener's stop().
    std::thread stopper {[&monitor] { monitor.stop("manual_stop"); }};
    {
        std::unique_lock lock(probe->gate);
        REQUIRE(probe->gateCv.wait_for(lock, std::chrono::seconds {2},
                                       [&probe] { return probe->stopEntered; }));
    }

    // The sampler has been told to finish and, without the ordering, would take the (now null)
    // listener pointer and emit the final sample while the teardown is still in flight. Give it
    // ample time to do exactly that.
    std::this_thread::sleep_for(std::chrono::milliseconds {150});
    CHECK(finals.load() == 0);

    probe->release();
    stopper.join();

    CHECK(finals.load() == 1);
    CHECK(probe->stops.load() == 1);
    CHECK(monitor.endReason() == "manual_stop");

    std::error_code ec;
    std::filesystem::remove(stateFilePath, ec);
}

TEST_CASE("A run the server has closed stops its event listener", "[wifi][listener]")
{
    // The other self-ending path: a 410 or 404 on ingest retires the sampler, and the listener
    // must go with it, or it keeps posting into the very run the server just refused.
    auto probe = std::make_shared<ListenerProbe>();
    auto runClosed = std::make_shared<std::atomic_bool>(false);
    auto options = listenerOptions(std::chrono::seconds {300}, probe);
    options.runClosed = runClosed;
    const auto stateFilePath = options.stateFilePath;

    NetworkMonitor monitor {std::move(options), {}};
    REQUIRE(monitor.start());
    REQUIRE(probe->starts.load() == 1);

    runClosed->store(true, std::memory_order_release);

    CHECK(waitFor([&] { return probe->stops.load() == 1; }));
    CHECK_FALSE(monitor.isActive());
    CHECK(monitor.endReason() == "run_closed");

    monitor.stop("shutdown");
    CHECK(probe->stops.load() == 1);

    std::error_code ec;
    std::filesystem::remove(stateFilePath, ec);
}

// --- ethernet selection ----------------------------------------------------

TEST_CASE("The routed interface is parsed from ip route get", "[wifi][ethernet]")
{
    // Verbatim from Scorbitron-31871.
    CHECK(parseIpRouteInterface(
                  "1.1.1.1 via 192.168.168.1 dev eth0 src 192.168.168.149 uid 1000")
                  .value_or("")
          == "eth0");
    CHECK(parseIpRouteInterface("1.1.1.1 via 10.0.0.1 dev wlan0 src 10.0.0.5").value_or("")
          == "wlan0");
    CHECK_FALSE(parseIpRouteInterface("RTNETLINK answers: Network is unreachable").has_value());
}

namespace {

/// Stubs the two Scorbitrons: wlan0 associated AND eth0 cabled, eth0 holding the default route.
CommandRunner bothUpRoutedVia(const std::string &routedIface)
{
    return [routedIface](const std::string &cmd, const std::vector<std::string> &args) {
        const auto joined = [&args] {
            std::string s;
            for (const auto &a : args) {
                s += a + " ";
            }
            return s;
        }();

        if (cmd == "ip") {
            return CommandResult {0, "1.1.1.1 via 192.168.168.1 dev " + routedIface + " src 1.2.3.4"};
        }
        if (cmd == "iw" && joined.rfind("dev ", 0) == 0 && args.size() == 1) {
            return CommandResult {0, "Interface wlan0"};
        }
        if (cmd == "cat" && joined.find("operstate") != std::string::npos) {
            return CommandResult {0, "up"};
        }
        if (cmd == "cat" && joined.find("carrier") != std::string::npos) {
            return CommandResult {0, "1"};
        }
        if (cmd == "cat" && joined.find("speed") != std::string::npos) {
            return CommandResult {0, "100"};
        }
        if (cmd == "iw") {
            return CommandResult {0, "Connected to a2:05:d6:42:25:10 (on wlan0)\n\tSSID: TL_LOCAL\n"
                                     "\tfreq: 2437\n\tsignal: -59 dBm"};
        }
        return CommandResult {1, ""};
    };
}

} // namespace

TEST_CASE("iw dev lists wlan1 before wlan0 on a Scorbitron", "[wifi][ethernet]")
{
    // Verbatim from Scorbitron-31871. Order matters: wlan1 is the commissioning AP, reports
    // "Not connected." and has no /proc/net/wireless row, so taking the first entry yields an
    // entirely empty radio for a healthy link. linuxWifiInterface() therefore consults
    // /proc/net/wireless first and only falls back to this ordering.
    const auto ifaces = parseIwDevInterfaces("phy#0\n\tInterface wlan1\n\tInterface wlan0\n");

    REQUIRE(ifaces.size() == 2);
    CHECK(ifaces[0] == "wlan1");
    CHECK(ifaces[1] == "wlan0");
}

TEST_CASE("Ethernet is chosen by what carries the route, not by what exists", "[wifi][ethernet]")
{
    // The rule SB-3465 originally specified -- "both interfaces present -> Wi-Fi" -- is wrong on
    // both reference Scorbitrons: wlan0 is associated at -59 dBm while eth0 holds the
    // lower-metric default route, so the traffic leaves over the cable.
    CHECK(shouldSampleEthernet(std::optional<std::string> {"eth0"}, false));

    // Routed over a radio: sample Wi-Fi.
    CHECK_FALSE(shouldSampleEthernet(std::optional<std::string> {"wlan0"}, true));

    // No default route -- fall through to the Wi-Fi path rather than guessing Ethernet.
    CHECK_FALSE(shouldSampleEthernet(std::nullopt, false));
    CHECK_FALSE(shouldSampleEthernet(std::nullopt, true));
}

TEST_CASE("Wireless is detected from sysfs, not from iw", "[wifi][ethernet]")
{
    // uevent contents verbatim from Scorbitron-31871.
    const auto sysfs = [](const std::string &devtype, int exitCode) {
        return [devtype, exitCode](const std::string &, const std::vector<std::string> &) {
            return CommandResult {exitCode, devtype};
        };
    };

    CHECK(isWirelessInterface("wlan0", sysfs("DEVTYPE=wlan\n", 0)));
    CHECK_FALSE(isWirelessInterface("eth0", sysfs("INTERFACE=eth0\nIFINDEX=2\n", 0)));

    // The case this exists for: `iw` is only an AUTO package on our images (SB-3462), so the old
    // iw-based classification turned a missing tool into "this radio is Ethernet" -- reporting
    // source:ethernet with no RF data for a Wi-Fi device. sysfs cannot go missing, and an
    // unreadable read still answers "wireless", because mislabelling Wi-Fi as wired is worse than
    // the reverse.
    CHECK(isWirelessInterface("wlan0", sysfs("", 1)));
}

TEST_CASE("A wired link reports speed and leaves the Wi-Fi fields unset", "[wifi][ethernet]")
{
    // /sys values verbatim from Scorbitron-31871.
    const auto runner = [](const std::string &, const std::vector<std::string> &args) {
        const auto &path = args.front();
        if (path.find("operstate") != std::string::npos) {
            return CommandResult {0, "up\n"};
        }
        if (path.find("carrier") != std::string::npos) {
            return CommandResult {0, "1\n"};
        }
        if (path.find("speed") != std::string::npos) {
            return CommandResult {0, "100\n"};
        }
        return CommandResult {1, ""};
    };

    const auto link = collectEthernet("eth0", runner);
    REQUIRE(link.has_value());
    CHECK(link->kind == InterfaceKind::Ethernet);
    CHECK(link->interfaceName == "eth0");
    CHECK(link->connected);
    CHECK(link->linkRateMbps.value_or(0) == 100);

    // Wi-Fi-only fields must stay absent rather than carry the radio's numbers.
    CHECK_FALSE(link->rssiDbm.has_value());
    CHECK_FALSE(link->noiseDbm.has_value());
    CHECK_FALSE(link->txRetryPct.has_value());
    CHECK_FALSE(link->beaconLossCount.has_value());
    CHECK(link->ssid.empty());
    CHECK(link->bssid.empty());
}

TEST_CASE("A down wired link is reported disconnected, with no speed", "[wifi][ethernet]")
{
    const auto runner = [](const std::string &, const std::vector<std::string> &args) {
        const auto &path = args.front();
        if (path.find("operstate") != std::string::npos) {
            return CommandResult {0, "down"};
        }
        if (path.find("carrier") != std::string::npos) {
            return CommandResult {0, "0"};
        }
        if (path.find("speed") != std::string::npos) {
            return CommandResult {0, "-1"}; // what the kernel reports on a down link
        }
        return CommandResult {1, ""};
    };

    const auto link = collectEthernet("eth0", runner);
    REQUIRE(link.has_value());
    CHECK_FALSE(link->connected);
    CHECK_FALSE(link->linkRateMbps.has_value());
}
