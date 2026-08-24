/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 */

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cstddef>
#include <string>

#include "identifiers.h"

using namespace scorbit::detail;

namespace {

// Longest realistic substitutions, used to size the NFC tag against the probe's limit.
constexpr auto MACHINE_UUID = "3f2504e0-4f89-11d3-9a0c-0305e82c3301";

/// Mirrors MAX_NFC_TAG_URI_LENGTH in net.cpp. ProbeNFC::SetUri() writes the URI length as a
/// uint8_t, so a longer URI is silently truncated on the wire.
constexpr std::size_t NFC_URI_LIMIT = 255;

} // namespace

TEST_CASE("PAIRING_DEEPLINK carries UTM attribution")
{
    const auto url = fmt::format(PAIRING_DEEPLINK, fmt::arg("manufacturer_prefix", "stern"),
                                 fmt::arg("scorbit_machine_id", 4242),
                                 fmt::arg("scorbitron_uuid", MACHINE_UUID));

    CHECK(url
          == "https://scorbit.link/qrcode?$deeplink_path=stern&machineid=4242"
             "&uuid=3f2504e0-4f89-11d3-9a0c-0305e82c3301"
             "&utm_medium=qr_code&utm_source=machine_pricing_card&utm_campaign=machine_pairing");
}

TEST_CASE("URL_NFC_TAG carries UTM attribution")
{
    const auto url = fmt::format(URL_NFC_TAG, fmt::arg("machine_uuid", MACHINE_UUID),
                                 fmt::arg("nonce", "9f86d081884c7d65"));

    CHECK(url
          == "https://scorbit.link/machines/3f2504e0-4f89-11d3-9a0c-0305e82c3301"
             "?n=9f86d081884c7d65"
             "&utm_medium=nfc&utm_source=machine_nfc_tag&utm_campaign=anonymous_play_claim");
}

TEST_CASE("URL_NFC_TAG fits within the probe's single-byte URI length")
{
    // The nonce is server-issued, so this guards the headroom left for it rather than one
    // specific value. A regression here means Net::setNfcTag() starts skipping tag writes.
    const auto url =
            fmt::format(URL_NFC_TAG, fmt::arg("machine_uuid", MACHINE_UUID), fmt::arg("nonce", ""));

    CHECK(url.size() <= NFC_URI_LIMIT);
    CHECK(NFC_URI_LIMIT - url.size() >= 64);
}

TEST_CASE("URL_CLAIM_DEEPLINK carries UTM attribution")
{
    const auto url = fmt::format(URL_CLAIM_DEEPLINK, fmt::arg(ARG_MACHINE_UUID, MACHINE_UUID),
                                 fmt::arg(ARG_SCORE_ID, 201));

    CHECK(url
          == "https://scorbit.link/machines/3f2504e0-4f89-11d3-9a0c-0305e82c3301/?score_id=201"
             "&utm_medium=machine&utm_source=score_claim&utm_campaign=anonymous_play_claim");
}

TEST_CASE("NFC and claim deep links share a campaign but differ by medium and source")
{
    // Both links exist to get an unclaimed slot claimed; utm_medium/utm_source separate them.
    // Keeping the campaign identical is what makes them roll up as one flow in analytics.
    const std::string campaign {"utm_campaign=anonymous_play_claim"};

    const auto nfc = fmt::format(URL_NFC_TAG, fmt::arg("machine_uuid", MACHINE_UUID),
                                 fmt::arg("nonce", "abc"));
    const auto claim = fmt::format(URL_CLAIM_DEEPLINK, fmt::arg(ARG_MACHINE_UUID, MACHINE_UUID),
                                   fmt::arg(ARG_SCORE_ID, 1));

    CHECK(nfc.find(campaign) != std::string::npos);
    CHECK(claim.find(campaign) != std::string::npos);

    CHECK(nfc.find("utm_medium=nfc") != std::string::npos);
    CHECK(claim.find("utm_medium=machine") != std::string::npos);

    CHECK(nfc.find("utm_source=machine_nfc_tag") != std::string::npos);
    CHECK(claim.find("utm_source=score_claim") != std::string::npos);
}
