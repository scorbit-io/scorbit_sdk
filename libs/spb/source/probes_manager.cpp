/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2025
 *
 ****************************************************************************/

#include "spb/probes_manager.h"
#include "spb/Probe.h"
#include "spb/SLB.h"
#include "spb/Spike.h"
#include "list_usb_devices.h"

namespace spb {

constexpr auto CPU_PROBE_ID = "CPU";
constexpr auto DMD_PROBE_ID = "DMD";
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
            if (has_flag(probesSet, ProbeType::CPU) && probeInfo.Id == CPU_PROBE_ID) {
                m_cpu = std::make_shared<ProbeCPU>();
                m_cpu->Initialize(0, device);
                if (callback) {
                    callback(m_cpu.get(), device);
                }
            } else if (has_flag(probesSet, ProbeType::DMD) && probeInfo.Id == DMD_PROBE_ID) {
                m_dmd = std::make_shared<ProbeDMD>();
                m_dmd->Initialize(0, device);
                if (callback) {
                    callback(m_dmd.get(), device);
                }
            } else if (has_flag(probesSet, ProbeType::NFC) && probeInfo.Id == NFC_PROBE_ID) {
                m_nfc = std::make_shared<ProbeNFC>();
                m_nfc->Initialize(0, device);
                if (callback) {
                    callback(m_nfc.get(), device);
                }
            }
        }
    }

    // Try to initialize SAM
    if (has_flag(probesSet, ProbeType::SAM)) {
        m_sam = std::make_shared<SLB_Sam>();
        if (!m_sam->Initialize() || m_sam->GetPatchVersion().empty()) {
            m_sam.reset();
        }
    }

    // Try to initialize Spike
    if (has_flag(probesSet, ProbeType::SPIKE)) {
        m_spike = std::make_shared<Spike>();
        if (!m_spike->Initialize(pbspk2commPath.data())) {
            m_spike.reset();
        }
    }

    if (m_nfc) {
        // Set NFC to TAG mode
        ProbeNFC::NfcInformations_t pbi;
        if (m_nfc->GetNfcInformations(&pbi) && !(pbi.Flags & ProbeNFC::NfcFlags_t::Tag)) {
            m_nfc->SetType(ProbeNFC::NfcType_t::TAG);
        }
    }
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

} // namespace spb
