/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 */

#include <diagnostics/lan_ip.h>
#include <diagnostics/wifi/wifi_diagnostics.h>
#include <csignal>
#include <iostream>
#include <thread>

namespace {

volatile std::sig_atomic_t keepRunning = 1;

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

} // namespace

int main()
{
    using namespace std::chrono_literals;
    namespace diagnostics = scorbit::detail;
    namespace wifi = scorbit::detail::wifi;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::cout << "Primary LAN IP: " << valueOrUnknown(diagnostics::getPrimaryLanIp()) << '\n';
    std::cout << "Wi-Fi status will refresh every 5 seconds. Press Ctrl+C to stop.\n";

    while (keepRunning) {
        const auto link = wifi::collectLinkInfo();
        if (!link) {
            std::cout << "Wi-Fi: unavailable\n";
        } else {
            std::cout << "Wi-Fi: backend=" << valueOrUnknown(link->backend)
                      << " interface=" << valueOrUnknown(link->interfaceName)
                      << " connected=" << (link->connected ? "yes" : "no")
                      << " ssid=" << valueOrUnknown(link->ssid)
                      << " bssid=" << valueOrUnknown(link->bssid)
                      << " rssi=" << valueOrUnknown(link->rssiDbm, " dBm")
                      << " rate=" << valueOrUnknown(link->linkRateMbps, " Mbps")
                      << " channel=" << valueOrUnknown(link->channel) << '\n';
        }

        for (int i = 0; i < 5 && keepRunning; ++i) {
            std::this_thread::sleep_for(1s);
        }
    }

    return 0;
}
