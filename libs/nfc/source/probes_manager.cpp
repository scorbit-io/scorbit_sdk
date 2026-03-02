/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2025
 *
 ****************************************************************************/

#include "nfc/probes_manager.h"
#include "nfc/Probe.h"
#include "nfc/Discovery.h"
#include "list_usb_devices.h"

#include <fmt/format.h>

namespace nfc {

constexpr auto NFC_PROBE_ID = "NFC";

constexpr int NFC_SESSION_BRIGHTNESS = 0;
constexpr int NFC_IDLE_BRIGHTNESS = 30;

static bool has_flag(probe_t probesSet, ProbeType flag)
{
    return static_cast<probe_t>(probesSet) & static_cast<probe_t>(flag);
}

void ProbesManager::enumerate(probe_t probesSet, const std::string pbspk2commPath,
                              const ProbeDisplayCallback &callback)
{
    const auto devices = listUsbDevices();

    for (const auto &device : devices) {
        bool hasProbe = false;
        ProbeBase::ProbeInformations_t probeInfo;

        // Temporary probe has to be destroyed before continuing to initialize probe
        {
            ProbeBase probe;
            if (probe.Initialize(0, device)) {
                hasProbe = probe.GetInformations(&probeInfo) && !probeInfo.Id.empty();
            }
        }

        if (hasProbe) {
            if (has_flag(probesSet, ProbeType::NFC) && probeInfo.Id == NFC_PROBE_ID) {
                m_nfc = std::make_shared<ProbeNFC>();
                m_nfc->Initialize(0, device);
                if (callback) {
                    callback(m_nfc.get(), device);
                }
            }
        }
    }

    if (m_nfc) {
        // Set NFC to TAG mode
        ProbeNFC::NfcInformations_t pbi;
        if (m_nfc->GetNfcInformations(&pbi) && !(pbi.Flags & ProbeNFC::NfcFlags_t::Tag)) {
            m_nfc->SetType(ProbeNFC::NfcType_t::TAG);
        }
    }

    m_discovery = std::make_shared<NetworkDiscovery>();
}

auto ProbesManager::isNfcTagRead() const -> bool
{
    if (m_nfc) {
        ProbeNFC::NfcInformations_t nfcInfo;
        if (m_nfc->GetNfcInformations(&nfcInfo)) {
            return nfcInfo.LastId != 0;
        }
    }
    return false;
}

auto ProbesManager::setNfcTag(const std::string &tag) -> bool
{
    if (m_nfc) {
        return m_nfc->SetUri(tag);
    }
    return false;
}

auto ProbesManager::setNfcLeds(NfcLedMode mode) -> bool
{
    if (m_nfc) {
        switch (mode) {
        case NfcLedMode::Idle:
            return m_nfc->SetLedsBrightness(NFC_IDLE_BRIGHTNESS);
        case NfcLedMode::GameSession:
            return m_nfc->SetLedsBrightness(NFC_SESSION_BRIGHTNESS);
        default:
            return false;
        }
    }
    return false;
}

auto ProbesManager::setDiscoveryDescription(const std::string &description) -> bool
{
    return m_discovery->Initialize(description);
}

auto ProbesManager::probesBootReason(ProbeType probeType) -> std::optional<std::string>
{
    std::string probeName;
    std::shared_ptr<ProbeBase> probe;

    switch (probeType) {
    case ProbeType::NFC:
        probe = m_nfc;
        probeName = NFC_PROBE_ID;
        break;
    default:
        return fmt::format("Unsupported probe type: {}", static_cast<uint32_t>(probeType));
    }

    if (!probe) {
        return std::nullopt;
    }

    ProbeBase::ProbeInformations_t probeInfo;

    if (probe->GetInformations(&probeInfo) && !probeInfo.Id.empty()) {

        std::string_view bootReason = "unknown";
        switch (probeInfo.BootReason) {
        case ProbeBase::Unknown:
            bootReason = "Unknown";
            break;
        case ProbeBase::PowerOnReset:
            bootReason = "PowerOnReset";
            break;
        case ProbeBase::BootloaderRestart:
            bootReason = "BootloaderRestart";
            break;
        case ProbeBase::SoftRestart:
            bootReason = "SoftRestart";
            break;
        case ProbeBase::WatchdogTimer:
            bootReason = "WatchdogTimer";
            break;
        case ProbeBase::WatchdogTimerAcknoledged:
            bootReason = "WatchdogTimerAcknoledged";
            break;
        }

        return fmt::format("{} probe boot reason: {}, watchdog count: {}", probeName, bootReason,
                           probeInfo.WatchdogCount);
    }

    enumerate(probeType);

    return fmt::format("{} coudn't get probe info!", probeName);
}

} // namespace nfc
