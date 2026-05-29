/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
 *
 * MIT License
 */

#include <diagnostics/wifi/wifi_diagnostics.h>
#include <catch2/catch_test_macros.hpp>

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
    CHECK(parsed->ssid == "VenueWifi");
    CHECK(parsed->bssid == "a1:b2:c3:d4:e5:f6");
    CHECK(parsed->rssiDbm == -64);
    CHECK(parsed->linkRateMbps == 156);
    CHECK(parsed->channel == 149);
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
