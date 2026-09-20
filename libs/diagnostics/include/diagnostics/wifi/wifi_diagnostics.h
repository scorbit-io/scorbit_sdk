/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 */

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace scorbit {
namespace detail {
namespace wifi {

enum class InterfaceKind {
    Unknown,
    Wifi,
    Ethernet,
};

struct LinkInfo {
    InterfaceKind kind {InterfaceKind::Unknown};
    std::string backend;
    std::string interfaceName;
    bool connected {false};
    std::string ssid;
    std::string bssid;
    std::optional<int> rssiDbm;
    /// Noise floor. SNR is derived server-side as rssiDbm - noiseDbm, so an
    /// implausible value here is worse than none: see plausibleNoiseDbm().
    std::optional<int> noiseDbm;
    std::optional<int> linkRateMbps;
    std::optional<double> txRetryPct;
    std::optional<int> beaconLossCount;
    std::optional<int> freqMhz;
    std::optional<int> channel;
};

/**
 * The dependency_checks vocabulary.
 *
 * Both halves of this are UNVALIDATED end to end: the server stores dependency_checks as a bare
 * DictField, and the Backstage panel does `checks[key] || "unknown"`. So a typo in either a key
 * or a status is stored happily and renders as "unknown" with no error anywhere -- the same
 * silent-drop class as an unknown payload key. Use these constants, never string literals.
 *
 * The keys are exactly the six chips NetworkDiagnosticsLive.vue renders (its DEP_KEYS). Note this
 * deliberately DIFFERS from SPEC-0007 §136-137, which also names `tls443` and omits `link` and
 * `dhcp_gateway`. The panel is the only consumer, so the panel wins; the spec is corrected under
 * SB-4866. Anything sent outside this set is stored and never displayed.
 */
namespace dependency {

constexpr auto STATUS_OK {"ok"};
constexpr auto STATUS_BLOCKED {"blocked"};
constexpr auto STATUS_INTERMITTENT {"intermittent"};

constexpr auto KEY_LINK {"link"};
constexpr auto KEY_DHCP_GATEWAY {"dhcp_gateway"};
constexpr auto KEY_DNS {"dns"};
constexpr auto KEY_CLOCK {"clock"};
constexpr auto KEY_REST443 {"rest443"};
constexpr auto KEY_WSS443 {"wss443"};

/// A REST call that succeeded this recently means the transport is currently fine.
constexpr std::chrono::seconds REST_OK_WITHIN {120};
/// Older than this and the transport is treated as down rather than merely quiet.
constexpr std::chrono::seconds REST_BLOCKED_BEYOND {600};
/// Clock delta against server time beyond which the device's clock is reported as wrong.
constexpr std::chrono::seconds CLOCK_OK_WITHIN {30};

} // namespace dependency

/**
 * Passive liveness of the owner's ALREADY-OPEN connections.
 *
 * SPEC-0007 §136 requires these be read from existing state rather than fresh handshakes, because
 * they are sampled continuously -- opening a socket every 30s to ask whether sockets work would
 * be both wasteful and self-defeating. Every field is optional: an owner that cannot answer one
 * leaves it empty and the corresponding chip stays "unknown" rather than being guessed at.
 */
struct DependencySnapshot {
    /// Realtime (Centrifugo) connection currently established.
    std::optional<bool> realtimeConnected;
    /// Age of the most recent successful REST call, or nullopt if none has succeeded yet.
    std::optional<std::chrono::seconds> sinceLastRestSuccess;
    /// Signed difference between server-reported time and local time, from a REST response.
    std::optional<std::chrono::seconds> clockDelta;
};

/// Supplies a DependencySnapshot on demand. Must not perform I/O -- see DependencySnapshot.
using DependencyProvider = std::function<DependencySnapshot()>;

struct ProbeResult {
    std::string target;
    std::optional<int> rttMs;
    std::optional<double> lossPct;
};

struct Sample {
    std::chrono::system_clock::time_point ts {std::chrono::system_clock::now()};
    LinkInfo link;
    std::optional<ProbeResult> gateway;
    std::optional<ProbeResult> publicInternet;
    std::optional<ProbeResult> scorbit;
    /// Named per-dependency reachability, keyed by dependency::KEY_*. Absent keys render as
    /// "unknown"; see buildDependencyChecks().
    std::map<std::string, std::string> dependencyChecks;
    bool isFinal {false};
};

struct Event {
    std::chrono::system_clock::time_point ts {std::chrono::system_clock::now()};
    std::string kind;
    std::optional<int> reasonCode;
    std::string payloadJson;
};

struct State {
    std::string runId;
    std::chrono::system_clock::time_point startedAt;
    std::chrono::system_clock::time_point deadline;
};

struct CommandResult {
    int exitCode {-1};
    std::string output;
};

using CommandRunner =
        std::function<CommandResult(const std::string &, const std::vector<std::string> &)>;

CommandResult runCommand(const std::string &command, const std::vector<std::string> &args);

std::optional<LinkInfo> collectLinkInfo(CommandRunner runner = runCommand,
                                        std::string preferredInterface = {});
std::optional<ProbeResult> probeHost(const std::string &target, int count = 3,
                                     CommandRunner runner = runCommand);
std::optional<std::string> defaultGateway(CommandRunner runner = runCommand);

/// Interface carrying the default route to @p target, e.g. "eth0".
///
/// A Scorbitron is routinely associated to Wi-Fi AND cabled at once, with eth0 on a lower metric,
/// so picking by name would sample a healthy radio while the traffic leaves over the cable.
std::optional<std::string> defaultRouteInterface(const std::string &target,
                                                 CommandRunner runner = runCommand);

/// Parses `ip route get` output for the `dev <iface>` it selected.
std::optional<std::string> parseIpRouteInterface(std::string_view output);

/// Every wireless interface named by `iw dev`, in listed order.
std::vector<std::string> parseIwDevInterfaces(std::string_view output);

/// Whether @p iface is a radio, from sysfs `DEVTYPE=wlan`.
///
/// Deliberately not `iw dev`: `iw` is only an AUTO package on our images (SB-3462), so it can be
/// removed by an autoremove, and a missing tool must not turn a radio into "Ethernet".
/// Unreadable sysfs answers true, because mislabelling Wi-Fi as wired is the worse error.
bool isWirelessInterface(const std::string &iface, CommandRunner runner = runCommand);

/// Sample Ethernet only when there IS a routed interface and it is not a radio.
bool shouldSampleEthernet(const std::optional<std::string> &routedIface, bool routedIsWireless);

/// Link state for a wired @p iface, from /sys/class/net. Wi-Fi-only fields are left unset.
std::optional<LinkInfo> collectEthernet(const std::string &iface, CommandRunner runner = runCommand);

/**
 * Assemble dependency_checks from what the sampler already measured plus the owner's passive state.
 *
 * @param link      link state, for KEY_LINK.
 * @param gateway   the gateway ICMP result already collected this round, for KEY_DHCP_GATEWAY.
 * @param snapshot  the owner's passive connection state, for KEY_REST443/KEY_WSS443/KEY_CLOCK.
 * @param dnsOk     the coarse-cadence DNS probe result, or nullopt if it has not run yet.
 *
 * A key whose input is unavailable is OMITTED rather than guessed. The panel renders a missing key
 * as "unknown", which is the honest answer -- emitting "ok" or "blocked" on no evidence would put
 * a confident wrong colour in front of Support, the same mistake as forwarding a -256 noise floor.
 */
/**
 * Whether @p host resolves. This is the one ACTIVE dependency probe, run at Options::
 * dependencyInterval rather than every sample.
 *
 * Resolution only -- no connection is made. A venue that resolves but blocks 443 is a different
 * failure, and the rest443/wss443 chips already cover it from the owner's live connections.
 */
bool resolveHost(const std::string &host);

std::map<std::string, std::string> buildDependencyChecks(const LinkInfo &link,
                                                         const std::optional<ProbeResult> &gateway,
                                                         const DependencySnapshot &snapshot,
                                                         std::optional<bool> dnsOk);

int wifiChannelFromFrequency(int freqMhz);
std::optional<double> parsePercent(std::string_view value);
std::optional<LinkInfo> parseIwLink(std::string_view output, std::string interfaceName);
std::optional<LinkInfo> parseIwStationDump(std::string_view output, LinkInfo base = {});
std::optional<LinkInfo> parseProcNetWireless(std::string_view output, std::string interfaceName);
std::optional<LinkInfo> parseNmcliWifiList(std::string_view output);
std::optional<LinkInfo> parseIwconfig(std::string_view output, std::string interfaceName);
std::optional<LinkInfo> parseAirportInfo(std::string_view output);
std::optional<LinkInfo> parseWdutilInfo(std::string_view output);
std::optional<LinkInfo> parseIpconfigGetsummary(std::string_view output, std::string interfaceName);
std::optional<LinkInfo> parseNetworksetupAirportNetwork(std::string_view output,
                                                        std::string interfaceName);
std::optional<LinkInfo> parseNetshWlanInterfaces(std::string_view output);
std::optional<ProbeResult> parsePingOutput(std::string_view output, std::string target);
std::optional<std::string> parseDefaultGateway(std::string_view output);

std::string defaultStateFilePath();
bool writeStateFile(const std::string &path, const State &state);
std::optional<State> readStateFile(const std::string &path);
bool removeStateFile(const std::string &path);

} // namespace wifi
} // namespace detail
} // namespace scorbit
