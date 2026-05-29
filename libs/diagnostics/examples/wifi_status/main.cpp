/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 */

#include <diagnostics/lan_ip.h>
#include <diagnostics/wifi/network_monitor.h>
#include <diagnostics/wifi/wifi_diagnostics.h>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

volatile std::sig_atomic_t keepRunning = 1;
std::mutex outputMutex;

void onSignal(int)
{
    keepRunning = 0;
}

std::string valueOrUnknown(const std::optional<int> &value, const char *suffix = "")
{
    if (!value) {
        return "unknown";
    }
    return std::to_string(*value) + suffix;
}

std::string valueOrUnknown(const std::string &value)
{
    return value.empty() ? "unknown" : value;
}

std::string valueOrUnknown(const std::optional<double> &value, const char *suffix = "")
{
    if (!value) {
        return "unknown";
    }
    return std::to_string(*value) + suffix;
}

void printProbe(const char *label, const std::optional<scorbit::detail::wifi::ProbeResult> &probe)
{
    std::cout << " " << label << "_rtt=" << (probe ? valueOrUnknown(probe->rttMs, " ms") : "unknown")
              << " " << label << "_loss="
              << (probe ? valueOrUnknown(probe->lossPct, "%") : "unknown");
}

} // namespace

int main()
{
    using namespace std::chrono_literals;
    namespace diagnostics = scorbit::detail;
    namespace wifi = scorbit::detail::wifi;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::cout << "Primary LAN IP: " << valueOrUnknown(diagnostics::getPrimaryLanIp()) << '\n';
    std::cout << "NetworkMonitor will refresh Wi-Fi status every 5 seconds. Press Ctrl+C to stop.\n";

    wifi::NetworkMonitor::Options options;
    options.runId = "diagnostics_wifi_status_example";
    options.requestedDuration = std::chrono::hours {24};
    options.sampleInterval = 5s;
    options.probeInterval = 15s;
    options.scanEnabled = false;
    options.stateFilePath =
            (std::filesystem::temp_directory_path() / "scorbit_diagnostics_wifi_status.state").string();

    wifi::NetworkMonitor monitor {
            std::move(options),
            {
                    [](const wifi::Sample &sample) {
                        std::scoped_lock lock(outputMutex);
                        const auto &link = sample.link;
                        std::cout << "NetworkMonitor sample: backend=" << valueOrUnknown(link.backend)
                                  << " interface=" << valueOrUnknown(link.interfaceName)
                                  << " connected=" << (link.connected ? "yes" : "no")
                                  << " ssid=" << valueOrUnknown(link.ssid)
                                  << " bssid=" << valueOrUnknown(link.bssid)
                                  << " rssi=" << valueOrUnknown(link.rssiDbm, " dBm")
                                  << " rate=" << valueOrUnknown(link.linkRateMbps, " Mbps")
                                  << " channel=" << valueOrUnknown(link.channel);
                        printProbe("gateway", sample.gateway);
                        printProbe("public", sample.publicInternet);
                        printProbe("scorbit", sample.scorbit);
                        std::cout << " final=" << (sample.isFinal ? "yes" : "no") << '\n';
                    },
                    [](const wifi::Event &event) {
                        std::scoped_lock lock(outputMutex);
                        std::cout << "NetworkMonitor event: kind=" << valueOrUnknown(event.kind)
                                  << " payload=" << valueOrUnknown(event.payloadJson) << '\n';
                    },
            }};

    if (!monitor.start()) {
        std::cerr << "NetworkMonitor failed to start\n";
        return 1;
    }

    while (keepRunning) {
        for (int i = 0; i < 5 && keepRunning; ++i) {
            std::this_thread::sleep_for(1s);
        }
    }

    monitor.stop("manual_stop");
    return 0;
}
