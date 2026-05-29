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
    std::optional<int> linkRateMbps;
    std::optional<double> txRetryPct;
    std::optional<int> beaconLossCount;
    std::optional<int> freqMhz;
    std::optional<int> channel;
};

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

int wifiChannelFromFrequency(int freqMhz);
std::optional<double> parsePercent(std::string_view value);
std::optional<LinkInfo> parseIwLink(std::string_view output, std::string interfaceName);
std::optional<LinkInfo> parseIwStationDump(std::string_view output, LinkInfo base = {});
std::optional<LinkInfo> parseProcNetWireless(std::string_view output, std::string interfaceName);
std::optional<LinkInfo> parseNmcliWifiList(std::string_view output);
std::optional<LinkInfo> parseIwconfig(std::string_view output, std::string interfaceName);
std::optional<LinkInfo> parseAirportInfo(std::string_view output);
std::optional<LinkInfo> parseWdutilInfo(std::string_view output);
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
