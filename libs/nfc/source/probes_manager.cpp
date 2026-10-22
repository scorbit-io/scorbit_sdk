/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2025
 *
 ****************************************************************************/

// This define necessary for NFC probe (before include Probe.h)
#define USE_PROBE_NFC

#include "nfc/probes_manager.h"
#include "nfc/Probe.h"
#include "nfc/ListUsbDevices.h"

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace nfc {

constexpr auto NFC_PROBE_ID = "NFC";

constexpr int NFC_SESSION_BRIGHTNESS = 0;
constexpr int NFC_IDLE_BRIGHTNESS = 30;

static_assert(NFC_BOOT_REASON_COUNT == ProbeBase::WatchdogTimerAcknoledged + 1,
              "NFC_BOOT_REASON_COUNT must match the vendored firmware boot reasons");

static bool has_flag(probe_t probesSet, ProbeType flag)
{
    return static_cast<probe_t>(probesSet) & static_cast<probe_t>(flag);
}

// Same numeric rules as HardwareTpm::isValid (Scorbit-assigned device serial)
static bool isValidScorbitSerial(uint64_t serialNumber)
{
    if (serialNumber < 15004) {
        return false;
    }
    auto n = serialNumber;
    int sum = 0;
    while (n != 0) {
        sum += static_cast<int>(n % 10);
        n /= 10;
    }
    return sum % 10 == 0;
}

namespace {

/** Production NfcProbeDevice over the USB ProbeNFC driver. */
class ProbeNfcDevice final : public NfcProbeDevice
{
public:
    explicit ProbeNfcDevice(std::unique_ptr<ProbeNFC> probe)
        : m_probe {std::move(probe)}
    {
    }

    bool getInformations(ProbeInformation &info) override
    {
        ProbeBase::ProbeInformations_t pbi {};
        if (!m_probe->GetInformations(&pbi) || pbi.Id.empty()) {
            return false;
        }

        info.id = pbi.Id;
        info.name = pbi.Name;
        info.versionMajor = static_cast<uint8_t>(pbi.VersionMajor);
        info.versionMinor = static_cast<uint8_t>(pbi.VersionMinor);
        info.versionRevision = static_cast<uint16_t>(pbi.VersionRevision);
        info.timestampHeader = pbi.TimestampHeader;
        info.timestampUtc = pbi.TimestampUTC;
        info.timestamp = pbi.Timestamp;
        info.bootReason = static_cast<uint8_t>(pbi.BootReason);
        info.watchdogCount = pbi.WatchdogCount;
        info.timeMs = static_cast<uint32_t>(pbi.TimeMs);
        info.sysClockFrequency = pbi.SysClockFrequency;
        info.usbFlags = pbi.UsbFlags;
        info.uid = pbi.UID;
        std::copy(std::begin(pbi.TpmSerial), std::end(pbi.TpmSerial), info.tpmSerial.begin());
        info.tpmType = pbi.TpmType;
        std::copy(std::begin(pbi.TpmUUID), std::end(pbi.TpmUUID), info.tpmUuid.begin());
        info.tpmScorbitSerial = pbi.TpmScorbitSerial;
        return true;
    }

    bool getNfcInformations(NfcInformation &info) override
    {
        ProbeNFC::NfcInformations_t nfcInfo {};
        if (!m_probe->GetNfcInformations(&nfcInfo)) {
            return false;
        }

        info.flags = static_cast<uint8_t>(nfcInfo.Flags);
        info.brightness = nfcInfo.Brightness;
        info.capacitance = nfcInfo.Capacitance;
        info.lastId = nfcInfo.LastId;
        info.tagUri = nfcInfo.TagUri;
        info.rssi = nfcInfo.RSSI;
        return true;
    }

    bool setUri(const std::string &uri) override { return m_probe->SetUri(uri); }

    bool setLedsBrightness(uint8_t brightness) override
    {
        return m_probe->SetLedsBrightness(brightness);
    }

    bool setType(NfcType type) override
    {
        return m_probe->SetType(type == NfcType::Tag ? ProbeNFC::NfcType_t::TAG
                                                     : ProbeNFC::NfcType_t::READER);
    }

private:
    std::unique_ptr<ProbeNFC> m_probe;
};

} // namespace

/** Production NfcProbeScanner: scan USB for an NFC probe (the last one found wins). */
static std::shared_ptr<NfcProbeDevice> scanUsbForNfcProbe(probe_t probesSet,
                                                          const ProbeDisplayCallback &callback)
{
    std::shared_ptr<NfcProbeDevice> found;
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
                auto probe = std::make_unique<ProbeNFC>();
                if (!probe->Initialize(0, device)) {
                    continue; // never hand back an unusable handle in place of a working one
                }
                if (isValidScorbitSerial(probeInfo.TpmScorbitSerial)) {
                    // Just in case clear factory mode bit
                    probe->SetFlags(static_cast<uint8_t>(ProbeNFC::NfcFlags_t::FactoryMode),
                                    ProbeNFC::NfcFlags_t::None);
                }
                if (callback) {
                    callback(probe.get(), device);
                }
                found = std::make_shared<ProbeNfcDevice>(std::move(probe));
            }
        }
    }

    return found;
}

ProbesManager::ProbesManager()
    : m_scanner {scanUsbForNfcProbe}
{
}

ProbesManager::ProbesManager(std::shared_ptr<NfcProbeDevice> nfcDevice, NfcProbeScanner scanner)
    : m_scanner {std::move(scanner)}
    , m_nfc {std::move(nfcDevice)}
{
}

void ProbesManager::enumerate(probe_t probesSet, const std::string pbspk2commPath,
                              const ProbeDisplayCallback &callback)
{
    (void)pbspk2commPath;
    std::scoped_lock lock {m_mutex};
    enumerateLocked(probesSet, callback);
}

auto ProbesManager::rescanNfc() -> void
{
    std::scoped_lock lock {m_mutex};
    enumerateLocked(ProbeType::NFC, nullptr, true);
}

void ProbesManager::enumerateLocked(probe_t probesSet, const ProbeDisplayCallback &callback,
                                    bool forceRescan)
{
    const bool isRescan = forceRescan || m_nfc != nullptr;

    if (m_scanner) {
        if (auto found = m_scanner(probesSet, callback)) {
            m_nfc = std::move(found);
            m_appliedLedMode = NO_LED_MODE; // a new device has not been told the mode yet
        }
    }

    if (m_nfc) {
        // Set NFC to TAG mode
        NfcInformation info;
        if (m_nfc->getNfcInformations(info)) {
            recordSeenTapLocked(info.lastId);
            // That read cleared the tap indicator. Keep a tap seen on a rescan; at startup the
            // SDK has no nonces yet, so a stale tap is dropped (but still recorded as seen).
            if (isRescan && info.lastId != 0) {
                ++m_pendingTaps;
            }
            if (!(info.flags & NfcFlagTag)) {
                m_nfc->setType(NfcType::Tag);
            }
        }
    }

    // Keep an existing discovery: setDiscoveryDescription() skips unchanged info, so a fresh,
    // uninitialized one would silently stop LAN advertising after a rescan.
    if (!m_discovery) {
        m_discovery = std::make_shared<NetworkDiscovery>();
    }
}

auto ProbesManager::hasNfc() const -> bool
{
    std::scoped_lock lock {m_mutex};
    return m_nfc != nullptr;
}

auto ProbesManager::isNfcTagRead() -> bool
{
    // Runs on the async timer executor, which must never block. If a (possibly slow) rescan
    // holds the probe, skip this poll: queued taps persist and the pad keeps its own tap for
    // the next one.
    std::unique_lock lock {m_mutex, std::try_to_lock};
    if (!lock.owns_lock()) {
        return false;
    }

    // An LED change the host thread could not apply (lock was busy) or that failed
    applyLedModeLocked();

    // Read the pad on every poll, even with taps queued: it holds only one read-and-clear tap,
    // so skipping a read would let a later tap overwrite an unread one.
    if (m_nfc) {
        NfcInformation nfcInfo;
        if (m_nfc->getNfcInformations(nfcInfo)) {
            recordSeenTapLocked(nfcInfo.lastId);
            if (nfcInfo.lastId != 0) {
                ++m_pendingTaps;
            }
        }
    }

    if (m_pendingTaps > 0) {
        --m_pendingTaps;
        return true;
    }
    return false;
}

auto ProbesManager::setNfcTag(const std::string &tag) -> bool
{
    std::scoped_lock lock {m_mutex};
    if (m_nfc) {
        return m_nfc->setUri(tag);
    }
    return false;
}

auto ProbesManager::setNfcLeds(NfcLedMode mode) -> bool
{
    if (mode != NfcLedMode::Idle && mode != NfcLedMode::GameSession) {
        return false;
    }
    m_desiredLedMode.store(static_cast<int>(mode));

    // Called on the host thread: never wait on a probe busy with a (possibly slow) rescan. If
    // the lock is taken, the next isNfcTagRead() poll applies the mode.
    std::unique_lock lock {m_mutex, std::try_to_lock};
    if (!lock.owns_lock()) {
        return true;
    }
    if (!m_nfc) {
        return false;
    }
    applyLedModeLocked();
    return true;
}

void ProbesManager::applyLedModeLocked()
{
    const int desired = m_desiredLedMode.load();
    if (!m_nfc || desired == NO_LED_MODE || desired == m_appliedLedMode) {
        return;
    }

    int brightness = 0;
    switch (static_cast<NfcLedMode>(desired)) {
    case NfcLedMode::Idle:
        brightness = NFC_IDLE_BRIGHTNESS;
        break;
    case NfcLedMode::GameSession:
        brightness = NFC_SESSION_BRIGHTNESS;
        break;
    default:
        return;
    }

    // On failure m_appliedLedMode stays stale, so the next poll retries
    if (m_nfc->setLedsBrightness(static_cast<uint8_t>(brightness))) {
        m_appliedLedMode = desired;
    }
}

auto ProbesManager::setDiscoveryDescription(uint64_t providerSerial, const std::string &gameName,
                                            const std::string &providerInfo,
                                            const std::string &extraInfo) -> bool
{
    NetworkDiscovery::DeviceInfo_t info;
    info.ProviderSerial = providerSerial;
    info.GameName = gameName;
    info.ProviderInfo = providerInfo;
    info.ExtraInfo = extraInfo;

    // m_discovery is created by enumerate(); lock so a concurrent (re)scan never races this
    std::scoped_lock lock {m_mutex};

    if (!m_discovery) {
        return false;
    }

    // Skip setting if same info
    if (m_discoveryInfo.GameName == info.GameName
        && m_discoveryInfo.ProviderInfo == info.ProviderInfo
        && m_discoveryInfo.ExtraInfo == info.ExtraInfo
        && m_discoveryInfo.ProviderSerial == info.ProviderSerial) {
        return true;
    }

    m_discoveryInfo = info;
    return m_discovery->Initialize(m_discoveryInfo);
}

auto ProbesManager::probesBootReason(ProbeType probeType, std::optional<ProbeInformation> *identity)
        -> std::optional<std::string>
{
    std::string probeName;

    switch (probeType) {
    case ProbeType::NFC:
        probeName = NFC_PROBE_ID;
        break;
    default:
        return fmt::format("Unsupported probe type: {}", static_cast<uint32_t>(probeType));
    }

    std::scoped_lock lock {m_mutex};

    if (!m_nfc) {
        return std::nullopt;
    }

    ProbeInformation probeInfo;

    if (m_nfc->getInformations(probeInfo)) {
        if (identity) {
            *identity = probeInfo;
        }

        std::string_view bootReason = "unknown";
        switch (probeInfo.bootReason) {
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
            bootReason = "WatchdogTimerAcknowledged";
            break;
        }

        return fmt::format("{} probe boot reason: {}, watchdog count: {}", probeName, bootReason,
                           probeInfo.watchdogCount);
    }

    enumerateLocked(probeType, nullptr);

    return fmt::format("{} couldn't get probe info!", probeName);
}

auto ProbesManager::nfcProbeInfo() -> NfcProbeInfoResult
{
    NfcProbeInfoResult result;

    std::scoped_lock lock {m_mutex};

    if (!m_nfc) {
        result.status = NfcProbeInfoResult::Status::NotPresent;
        return result;
    }

    // ReadNfcInfos clears the tap indicator: keep any tap it reports for isNfcTagRead()
    const bool nfcOk = m_nfc->getNfcInformations(result.nfc);
    if (nfcOk && result.nfc.lastId != 0) {
        ++m_pendingTaps;
        recordSeenTapLocked(result.nfc.lastId);
    }

    const bool probeOk = m_nfc->getInformations(result.probe);

    result.status = (nfcOk && probeOk) ? NfcProbeInfoResult::Status::Ok
                                       : NfcProbeInfoResult::Status::ReadFailed;
    result.lastSeenTapId = m_lastSeenTapId;
    result.lastSeenTapUnixMs = m_lastSeenTapUnixMs;
    return result;
}

void ProbesManager::recordSeenTapLocked(uint32_t tapId)
{
    if (tapId == 0) {
        return;
    }
    m_lastSeenTapId = tapId;
    m_lastSeenTapUnixMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
}

} // namespace nfc
