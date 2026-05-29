/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 */

#include <diagnostics/wifi/wifi_diagnostics.h>
#include <nlohmann/json.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>

namespace scorbit {
namespace detail {
namespace wifi {
namespace {

std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string {value.substr(first, last - first + 1)};
}

std::string lower(std::string value)
{
    std::ranges::transform(value, value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

void addBackend(LinkInfo &info, std::string_view backend)
{
    if (backend.empty()) {
        return;
    }
    if (!info.backend.empty()) {
        info.backend += "+";
    }
    info.backend += backend;
}

std::optional<int> parseInt(std::string_view value)
{
    const auto text = trim(value);
    int parsed = 0;
    const auto *begin = text.data();
    const auto *end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec == std::errc {}) {
        return parsed;
    }
    return std::nullopt;
}

std::optional<double> parseDouble(std::string_view value)
{
    const auto text = trim(value);
    if (text.empty()) {
        return std::nullopt;
    }
    char *end = nullptr;
    const auto parsed = std::strtod(text.c_str(), &end);
    if (end != text.c_str()) {
        return parsed;
    }
    return std::nullopt;
}

std::optional<std::string> matchString(std::string_view output, const std::regex &re,
                                       size_t group = 1)
{
    std::cmatch match;
    if (std::regex_search(output.data(), output.data() + output.size(), match, re)
        && match.size() > group) {
        return trim(match[group].str());
    }
    return std::nullopt;
}

std::optional<int> probeTcpConnectMs(std::string_view target, std::string_view service)
{
    using boost::asio::ip::tcp;
    using clock = std::chrono::steady_clock;

    boost::asio::io_context io;
    tcp::resolver resolver {io};
    tcp::socket socket {io};
    boost::asio::steady_timer timer {io};
    boost::system::error_code finalEc = boost::asio::error::operation_aborted;
    const auto started = clock::now();

    timer.expires_after(std::chrono::milliseconds {1500});
    timer.async_wait([&](const boost::system::error_code &ec) {
        if (!ec) {
            boost::system::error_code ignored;
            resolver.cancel();
            socket.cancel(ignored);
            socket.close(ignored);
        }
    });

    resolver.async_resolve(
            std::string {target}, std::string {service},
            [&](const boost::system::error_code &ec, const tcp::resolver::results_type &results) {
                if (ec) {
                    finalEc = ec;
                    timer.cancel();
                    return;
                }

                boost::asio::async_connect(
                        socket, results,
                        [&](const boost::system::error_code &connectEc, const tcp::endpoint &) {
                            finalEc = connectEc;
                            timer.cancel();
                        });
            });

    io.run();
    if (finalEc) {
        return std::nullopt;
    }

    return static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - started).count());
}

std::optional<int> matchInt(std::string_view output, const std::regex &re, size_t group = 1)
{
    if (const auto value = matchString(output, re, group); value) {
        return parseInt(*value);
    }
    return std::nullopt;
}

std::optional<double> matchDouble(std::string_view output, const std::regex &re, size_t group = 1)
{
    if (const auto value = matchString(output, re, group); value) {
        return parseDouble(*value);
    }
    return std::nullopt;
}

std::vector<std::string> lines(std::string_view output)
{
    std::vector<std::string> result;
    std::istringstream stream {std::string {output}};
    std::string line;
    while (std::getline(stream, line)) {
        result.push_back(line);
    }
    return result;
}

std::string shellQuote(const std::string &value)
{
#ifdef _WIN32
    std::string quoted = "\"";
    for (const char c : value) {
        if (c == '"') {
            quoted += "\\\"";
        } else {
            quoted += c;
        }
    }
    quoted += "\"";
    return quoted;
#else
    std::string quoted = "'";
    for (const char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
#endif
}

std::string commandLine(const std::string &command, const std::vector<std::string> &args)
{
    std::string line = shellQuote(command);
    for (const auto &arg : args) {
        line += " ";
        line += shellQuote(arg);
    }
    line += " 2>&1";
    return line;
}

[[maybe_unused]] std::optional<std::string> linuxWifiInterface(CommandRunner runner)
{
    if (const auto *env = std::getenv("SCORBIT_WIFI_INTERFACE"); env != nullptr && *env != '\0') {
        return std::string {env};
    }

    const auto iw = runner("iw", {"dev"});
    if (iw.exitCode == 0) {
        static const std::regex ifaceRe {R"(\bInterface\s+([^\s]+))"};
        if (const auto iface = matchString(iw.output, ifaceRe); iface) {
            return iface;
        }
    }

    const auto proc = runner("cat", {"/proc/net/wireless"});
    if (proc.exitCode == 0) {
        for (const auto &line : lines(proc.output)) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) {
                return trim(std::string_view {line}.substr(0, colon));
            }
        }
    }

    return std::nullopt;
}

[[maybe_unused]] std::optional<LinkInfo> collectLinux(CommandRunner runner,
                                                      std::string preferredInterface)
{
    auto iface = preferredInterface.empty() ? linuxWifiInterface(runner)
                                            : std::optional<std::string> {preferredInterface};
    if (!iface) {
        return std::nullopt;
    }

    LinkInfo info;
    info.kind = InterfaceKind::Wifi;
    info.interfaceName = *iface;

    if (const auto iw = runner("iw", {"dev", *iface, "link"}); iw.exitCode == 0) {
        if (auto parsed = parseIwLink(iw.output, *iface); parsed) {
            info = *parsed;
        }
    }

    if (const auto wireless = runner("cat", {"/proc/net/wireless"}); wireless.exitCode == 0) {
        if (auto parsed = parseProcNetWireless(wireless.output, *iface); parsed) {
            if (!info.rssiDbm) {
                info.rssiDbm = parsed->rssiDbm;
            }
            if (info.interfaceName.empty()) {
                info.interfaceName = parsed->interfaceName;
            }
            addBackend(info, parsed->backend);
        }
    }

    if (const auto station = runner("iw", {"dev", *iface, "station", "dump"});
        station.exitCode == 0) {
        if (auto parsed = parseIwStationDump(station.output, info); parsed) {
            info = *parsed;
            addBackend(info, "iw_station_dump");
        }
    }

    if (!info.connected || info.ssid.empty() || info.bssid.empty()) {
        if (const auto nmcli = runner("nmcli", {"-t", "-f", "active,ssid,bssid,chan,rate,signal",
                                                "dev", "wifi", "list", "--rescan", "no"});
            nmcli.exitCode == 0) {
            if (auto parsed = parseNmcliWifiList(nmcli.output); parsed) {
                if (info.ssid.empty()) {
                    info.ssid = parsed->ssid;
                }
                if (info.bssid.empty()) {
                    info.bssid = parsed->bssid;
                }
                if (!info.channel) {
                    info.channel = parsed->channel;
                }
                if (!info.linkRateMbps) {
                    info.linkRateMbps = parsed->linkRateMbps;
                }
                if (!info.rssiDbm) {
                    info.rssiDbm = parsed->rssiDbm;
                }
                addBackend(info, parsed->backend);
                info.connected = info.connected || parsed->connected;
            }
        }
    }

    if (!info.connected || info.ssid.empty() || info.bssid.empty()) {
        if (const auto iwconfig = runner("iwconfig", {*iface}); iwconfig.exitCode == 0) {
            if (auto parsed = parseIwconfig(iwconfig.output, *iface); parsed) {
                if (info.ssid.empty()) {
                    info.ssid = parsed->ssid;
                }
                if (info.bssid.empty()) {
                    info.bssid = parsed->bssid;
                }
                if (!info.linkRateMbps) {
                    info.linkRateMbps = parsed->linkRateMbps;
                }
                if (!info.rssiDbm) {
                    info.rssiDbm = parsed->rssiDbm;
                }
                if (!info.freqMhz) {
                    info.freqMhz = parsed->freqMhz;
                }
                if (!info.channel && info.freqMhz) {
                    info.channel = wifiChannelFromFrequency(*info.freqMhz);
                }
                addBackend(info, parsed->backend);
                info.connected = info.connected || parsed->connected;
            }
        }
    }

    return info;
}

[[maybe_unused]] std::optional<LinkInfo> collectMacos(CommandRunner runner)
{
    const std::vector<std::string> airportPaths {
            "/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/"
            "airport",
            "airport",
    };

    for (const auto &path : airportPaths) {
        const auto airport = runner(path, {"-I"});
        if (airport.exitCode == 0) {
            if (auto parsed = parseAirportInfo(airport.output); parsed) {
                return parsed;
            }
        }
    }

    const auto wdutil = runner("wdutil", {"info"});
    if (wdutil.exitCode == 0) {
        return parseWdutilInfo(wdutil.output);
    }

    return std::nullopt;
}

[[maybe_unused]] std::optional<LinkInfo> collectWindows(CommandRunner runner)
{
    const auto netsh = runner("netsh", {"wlan", "show", "interfaces"});
    if (netsh.exitCode == 0) {
        return parseNetshWlanInterfaces(netsh.output);
    }
    return std::nullopt;
}

std::chrono::system_clock::time_point parseIso8601Utc(const std::string &value)
{
    std::tm tm {};
    std::istringstream stream {value};
    stream >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (stream.fail()) {
        return {};
    }
#ifdef _WIN32
    const auto seconds = _mkgmtime(&tm);
#else
    const auto seconds = timegm(&tm);
#endif
    return std::chrono::system_clock::from_time_t(seconds);
}

std::string toIso8601Utc(std::chrono::system_clock::time_point tp)
{
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    const std::tm tm = *std::gmtime(&t);

    std::ostringstream stream;
    stream << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

} // namespace

CommandResult runCommand(const std::string &command, const std::vector<std::string> &args)
{
    const auto line = commandLine(command, args);
#ifdef _WIN32
    FILE *pipe = _popen(line.c_str(), "r");
#else
    FILE *pipe = popen(line.c_str(), "r");
#endif
    if (pipe == nullptr) {
        return {-1, {}};
    }

    std::string output;
    std::array<char, 4096> buffer {};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

#ifdef _WIN32
    const auto code = _pclose(pipe);
#else
    const auto code = pclose(pipe);
#endif
    return {code, output};
}

std::optional<LinkInfo> collectLinkInfo(CommandRunner runner, std::string preferredInterface)
{
#ifdef _WIN32
    (void)preferredInterface;
    return collectWindows(std::move(runner));
#elif defined(__APPLE__)
    (void)preferredInterface;
    return collectMacos(std::move(runner));
#elif defined(__linux__)
    return collectLinux(std::move(runner), std::move(preferredInterface));
#else
    (void)runner;
    (void)preferredInterface;
    return std::nullopt;
#endif
}

std::optional<ProbeResult> probeHost(const std::string &target, int count, CommandRunner runner)
{
    if (target.empty()) {
        return std::nullopt;
    }

#ifdef _WIN32
    const auto result = runner("ping", {"-n", std::to_string(count), "-w", "1000", target});
#elif defined(__APPLE__)
    const auto result = runner("ping", {"-c", std::to_string(count), "-W", "1000", target});
#else
    const auto result = runner("ping", {"-c", std::to_string(count), "-W", "1", target});
#endif
    if (result.output.empty()) {
        // Fall through to non-privileged TCP probing below.
    } else if (auto parsed = parsePingOutput(result.output, target); parsed) {
        return parsed;
    }

    for (const auto *service : {"443", "80", "53"}) {
        if (auto rttMs = probeTcpConnectMs(target, service); rttMs) {
            return ProbeResult {target, rttMs, 0.0};
        }
    }

    return std::nullopt;
}

std::optional<std::string> defaultGateway(CommandRunner runner)
{
#ifdef _WIN32
    const auto output = runner("route", {"print", "0.0.0.0"});
#elif defined(__APPLE__)
    const auto output = runner("route", {"-n", "get", "default"});
#else
    const auto output = runner("ip", {"route", "show", "default"});
#endif
    if (output.output.empty()) {
        return std::nullopt;
    }
    return parseDefaultGateway(output.output);
}

int wifiChannelFromFrequency(int freqMhz)
{
    if (freqMhz == 2484) {
        return 14;
    }
    if (freqMhz >= 2412 && freqMhz <= 2472) {
        return (freqMhz - 2407) / 5;
    }
    if (freqMhz >= 5000 && freqMhz <= 5900) {
        return (freqMhz - 5000) / 5;
    }
    if (freqMhz >= 5955 && freqMhz <= 7115) {
        return ((freqMhz - 5955) / 5) + 1;
    }
    return 0;
}

std::optional<double> parsePercent(std::string_view value)
{
    auto text = trim(value);
    if (!text.empty() && text.back() == '%') {
        text.pop_back();
    }
    return parseDouble(text);
}

std::optional<LinkInfo> parseIwLink(std::string_view output, std::string interfaceName)
{
    LinkInfo info;
    info.kind = InterfaceKind::Wifi;
    info.backend = "iw";
    info.interfaceName = std::move(interfaceName);

    static const std::regex connectedRe {R"(Connected to\s+([0-9a-fA-F:]{17}))"};
    static const std::regex ssidRe {R"(\bSSID:\s*(.+))"};
    static const std::regex freqRe {R"(\bfreq:\s*(\d+))"};
    static const std::regex signalRe {R"(\bsignal:\s*(-?\d+))"};
    static const std::regex rateRe {R"(\btx bitrate:\s*([0-9]+(?:\.[0-9]+)?))"};

    if (auto bssid = matchString(output, connectedRe); bssid) {
        info.connected = true;
        info.bssid = *bssid;
    } else if (output.find("Not connected.") != std::string_view::npos) {
        info.connected = false;
    }

    if (auto ssid = matchString(output, ssidRe); ssid) {
        info.ssid = *ssid;
    }
    info.freqMhz = matchInt(output, freqRe);
    info.rssiDbm = matchInt(output, signalRe);
    if (auto rate = matchDouble(output, rateRe); rate) {
        info.linkRateMbps = static_cast<int>(*rate);
    }
    if (info.freqMhz) {
        const auto channel = wifiChannelFromFrequency(*info.freqMhz);
        if (channel != 0) {
            info.channel = channel;
        }
    }

    if (!info.connected && info.ssid.empty() && !info.rssiDbm) {
        return std::nullopt;
    }
    return info;
}

std::optional<LinkInfo> parseIwStationDump(std::string_view output, LinkInfo base)
{
    static const std::regex retriesRe {R"(\btx retries:\s*(\d+))"};
    static const std::regex packetsRe {R"(\btx packets:\s*(\d+))"};
    static const std::regex beaconLossRe {R"(\bbeacon loss:\s*(\d+))"};

    const auto retries = matchDouble(output, retriesRe);
    const auto packets = matchDouble(output, packetsRe);
    if (retries && packets && (*retries + *packets) > 0) {
        base.txRetryPct = (*retries / (*retries + *packets)) * 100.0;
    }
    base.beaconLossCount = matchInt(output, beaconLossRe);

    if (!base.txRetryPct && !base.beaconLossCount) {
        return std::nullopt;
    }
    return base;
}

std::optional<LinkInfo> parseProcNetWireless(std::string_view output, std::string interfaceName)
{
    for (const auto &line : lines(output)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }

        const auto name = trim(std::string_view {line}.substr(0, colon));
        if (name != interfaceName) {
            continue;
        }

        std::istringstream stream {std::string {std::string_view {line}.substr(colon + 1)}};
        std::string status;
        double link = 0.0;
        double level = 0.0;
        stream >> status >> link >> level;

        LinkInfo info;
        info.kind = InterfaceKind::Wifi;
        info.backend = "proc_net_wireless";
        info.interfaceName = std::move(interfaceName);
        info.connected = true;
        info.rssiDbm = static_cast<int>(level);
        return info;
    }

    return std::nullopt;
}

std::optional<LinkInfo> parseNmcliWifiList(std::string_view output)
{
    for (const auto &line : lines(output)) {
        if (!line.starts_with("yes:")) {
            continue;
        }

        std::vector<std::string> fields;
        std::string current;
        bool escaped = false;
        for (const char c : line) {
            if (escaped) {
                current.push_back(c);
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == ':') {
                fields.push_back(current);
                current.clear();
            } else {
                current.push_back(c);
            }
        }
        fields.push_back(current);

        if (fields.size() < 6) {
            continue;
        }

        LinkInfo info;
        info.kind = InterfaceKind::Wifi;
        info.backend = "nmcli";
        info.connected = true;
        info.ssid = fields[1];
        info.bssid = fields[2];
        info.channel = parseInt(fields[3]);
        info.linkRateMbps = parseInt(fields[4]);
        if (auto signal = parseInt(fields[5]); signal) {
            info.rssiDbm = (*signal / 2) - 100;
        }
        return info;
    }

    return std::nullopt;
}

std::optional<LinkInfo> parseIwconfig(std::string_view output, std::string interfaceName)
{
    LinkInfo info;
    info.kind = InterfaceKind::Wifi;
    info.backend = "iwconfig";
    info.interfaceName = std::move(interfaceName);

    static const std::regex essidRe {R"ESSID(ESSID:"([^"]*)")ESSID"};
    static const std::regex apRe {R"(Access Point:\s*([0-9a-fA-F:]{17}))"};
    static const std::regex freqRe {R"(Frequency:([0-9.]+)\s*GHz)"};
    static const std::regex rateRe {R"(Bit Rate[=:]([0-9.]+))"};
    static const std::regex signalRe {R"(Signal level=(-?\d+))"};

    if (auto ssid = matchString(output, essidRe); ssid) {
        info.ssid = *ssid;
    }
    if (auto bssid = matchString(output, apRe); bssid && *bssid != "00:00:00:00:00:00") {
        info.bssid = *bssid;
        info.connected = true;
    }
    if (auto ghz = matchDouble(output, freqRe); ghz) {
        info.freqMhz = static_cast<int>((*ghz * 1000.0) + 0.5);
        info.channel = wifiChannelFromFrequency(*info.freqMhz);
    }
    if (auto rate = matchDouble(output, rateRe); rate) {
        info.linkRateMbps = static_cast<int>(*rate);
    }
    info.rssiDbm = matchInt(output, signalRe);

    if (!info.connected && info.ssid.empty() && !info.rssiDbm) {
        return std::nullopt;
    }
    return info;
}

std::optional<LinkInfo> parseAirportInfo(std::string_view output)
{
    LinkInfo info;
    info.kind = InterfaceKind::Wifi;
    info.backend = "airport";
    info.interfaceName = "airport";
    info.ssid = matchString(output, std::regex {R"((^|\n)\s*SSID:\s*(.+))"}, 2).value_or("");
    info.bssid = matchString(output, std::regex {R"((^|\n)\s*BSSID:\s*([0-9a-fA-F:]{17}))"}, 2)
                         .value_or("");
    info.rssiDbm = matchInt(output, std::regex {R"((^|\n)\s*agrCtlRSSI:\s*(-?\d+))"}, 2);
    info.linkRateMbps = matchInt(output, std::regex {R"((^|\n)\s*lastTxRate:\s*(\d+))"}, 2);

    if (auto channel = matchInt(output, std::regex {R"((^|\n)\s*channel:\s*(\d+))"}, 2); channel) {
        info.channel = channel;
    }
    info.connected = !info.ssid.empty() || !info.bssid.empty();
    if (!info.connected && !info.rssiDbm) {
        return std::nullopt;
    }
    return info;
}

std::optional<LinkInfo> parseWdutilInfo(std::string_view output)
{
    LinkInfo info;
    info.kind = InterfaceKind::Wifi;
    info.backend = "wdutil";
    info.interfaceName = "wifi";
    info.ssid = matchString(output, std::regex {R"(\bSSID\s*:\s*(.+))"}).value_or("");
    info.bssid =
            matchString(output, std::regex {R"(\bBSSID\s*:\s*([0-9a-fA-F:]{17}))"}).value_or("");
    info.rssiDbm = matchInt(output, std::regex {R"(\bRSSI\s*:\s*(-?\d+))"});
    info.channel = matchInt(output, std::regex {R"(\bChannel\s*:\s*(\d+))"});
    info.linkRateMbps = matchInt(output, std::regex {R"(\bTx Rate\s*:\s*(\d+))"});
    info.connected = !info.ssid.empty() || !info.bssid.empty();
    if (!info.connected && !info.rssiDbm) {
        return std::nullopt;
    }
    return info;
}

std::optional<LinkInfo> parseNetshWlanInterfaces(std::string_view output)
{
    LinkInfo info;
    info.kind = InterfaceKind::Wifi;
    info.backend = "netsh";

    const auto state =
            lower(matchString(output, std::regex {R"(\bState\s*:\s*(.+))"}).value_or(""));
    info.connected = state.find("connected") != std::string::npos;
    info.interfaceName = matchString(output, std::regex {R"(\bName\s*:\s*(.+))"}).value_or("");
    info.ssid = matchString(output, std::regex {R"(\bSSID\s*:\s*(.+))"}).value_or("");
    info.bssid =
            matchString(output, std::regex {R"(\bBSSID\s*:\s*([0-9a-fA-F:]{17}))"}).value_or("");
    info.channel = matchInt(output, std::regex {R"(\bChannel\s*:\s*(\d+))"});
    if (auto signal = matchInt(output, std::regex {R"(\bSignal\s*:\s*(\d+)%?)"}); signal) {
        info.rssiDbm = (*signal / 2) - 100;
    }

    const auto rx = matchDouble(output, std::regex {R"(\bReceive rate \(Mbps\)\s*:\s*([0-9.]+))"});
    const auto tx = matchDouble(output, std::regex {R"(\bTransmit rate \(Mbps\)\s*:\s*([0-9.]+))"});
    if (rx || tx) {
        info.linkRateMbps = static_cast<int>(std::max(rx.value_or(0.0), tx.value_or(0.0)));
    }

    if (!info.connected && info.interfaceName.empty()) {
        return std::nullopt;
    }
    return info;
}

std::optional<ProbeResult> parsePingOutput(std::string_view output, std::string target)
{
    ProbeResult result;
    result.target = std::move(target);

    static const std::regex unixLossRe {R"(([0-9]+(?:\.[0-9]+)?)%\s*packet loss)"};
    static const std::regex winLossRe {R"(\(([0-9]+)%\s*loss\))", std::regex::icase};
    static const std::regex unixRttRe {R"(=\s*[0-9.]+/([0-9.]+)/[0-9.]+/[0-9.]+\s*ms)"};
    static const std::regex winAvgRe {R"(Average\s*=\s*(\d+)ms)", std::regex::icase};
    static const std::regex macRttRe {R"(=\s*[0-9.]+/([0-9.]+)/[0-9.]+\s*ms)"};

    if (auto loss = matchDouble(output, unixLossRe); loss) {
        result.lossPct = loss;
    } else if (auto loss = matchDouble(output, winLossRe); loss) {
        result.lossPct = loss;
    }

    if (auto avg = matchDouble(output, unixRttRe); avg) {
        result.rttMs = static_cast<int>(*avg + 0.5);
    } else if (auto avg = matchDouble(output, macRttRe); avg) {
        result.rttMs = static_cast<int>(*avg + 0.5);
    } else if (auto avg = matchInt(output, winAvgRe); avg) {
        result.rttMs = avg;
    }

    if (!result.lossPct && !result.rttMs) {
        return std::nullopt;
    }
    return result;
}

std::optional<std::string> parseDefaultGateway(std::string_view output)
{
    static const std::regex linuxRe {R"(\bdefault\s+via\s+([0-9a-fA-F:.]+))"};
    static const std::regex macRe {R"(\bgateway:\s*([0-9a-fA-F:.]+))"};
    static const std::regex winRe {
            R"(\s*0\.0\.0\.0\s+0\.0\.0\.0\s+([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+))"};

    if (auto gateway = matchString(output, linuxRe); gateway) {
        return gateway;
    }
    if (auto gateway = matchString(output, macRe); gateway) {
        return gateway;
    }
    if (auto gateway = matchString(output, winRe); gateway) {
        return gateway;
    }
    return std::nullopt;
}

std::string defaultStateFilePath()
{
#ifdef _WIN32
    if (const auto *programData = std::getenv("ProgramData");
        programData != nullptr && *programData != '\0') {
        return (std::filesystem::path {programData} / "Scorbit" / "network_monitor.state").string();
    }
    return "network_monitor.state";
#elif defined(__APPLE__)
    return "/var/lib/scorbitd/network_monitor.state";
#else
    return "/var/lib/scorbitd/network_monitor.state";
#endif
}

bool writeStateFile(const std::string &path, const State &state)
{
    try {
        const std::filesystem::path statePath {path};
        if (statePath.has_parent_path()) {
            std::filesystem::create_directories(statePath.parent_path());
        }

        nlohmann::json j {
                {"run_id", state.runId},
                {"started_at", toIso8601Utc(state.startedAt)},
                {"deadline", toIso8601Utc(state.deadline)},
        };

        std::ofstream file {path, std::ios::trunc};
        file << j.dump();
        return file.good();
    } catch (...) {
        return false;
    }
}

std::optional<State> readStateFile(const std::string &path)
{
    try {
        std::ifstream file {path};
        if (!file.is_open()) {
            return std::nullopt;
        }

        nlohmann::json j;
        file >> j;

        State state;
        state.runId = j.value("run_id", std::string {});
        state.startedAt = parseIso8601Utc(j.value("started_at", std::string {}));
        state.deadline = parseIso8601Utc(j.value("deadline", std::string {}));
        if (state.runId.empty() || state.deadline == std::chrono::system_clock::time_point {}) {
            return std::nullopt;
        }
        return state;
    } catch (...) {
        return std::nullopt;
    }
}

bool removeStateFile(const std::string &path)
{
    try {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return !ec;
    } catch (...) {
        return false;
    }
}

} // namespace wifi
} // namespace detail
} // namespace scorbit
