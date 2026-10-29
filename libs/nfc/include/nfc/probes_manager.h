/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2025
 *
 ****************************************************************************/

#pragma once

#include "nfc/Discovery.h"
#include "nfc/nfc_probe_device.h"

#include <atomic>
#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <optional>

class ProbeBase;

namespace nfc {

using probe_t = uint32_t;

enum ProbeType : probe_t {
    None = 0,
    NFC = 1u << 2,

    All = NFC
};

enum class NfcLedMode {
    Idle,
    GameSession,
};

using ProbeDisplayCallback = std::function<void(ProbeBase *probe, const std::string &device)>;

/** Finds an NFC probe (USB scan in production); nullptr when none is found. */
using NfcProbeScanner = std::function<std::shared_ptr<NfcProbeDevice>(
        probe_t probesSet, const ProbeDisplayCallback &callback)>;

/** Result of @ref ProbesManager::nfcProbeInfo. */
struct NfcProbeInfoResult {
    enum class Status {
        Ok,
        NotPresent,
        ReadFailed,
    };

    Status status {Status::NotPresent};
    ProbeInformation probe;
    NfcInformation nfc;
    /** Most recent non-zero tap id seen by any probe read since start; 0 if none. */
    uint32_t lastSeenTapId {0};
    /** When @ref lastSeenTapId was read, Unix epoch milliseconds; 0 if none. */
    uint64_t lastSeenTapUnixMs {0};
};

/**
 * Owns the connected probes. Every probe I/O and every replacement of the probe handle is
 * serialized by one internal mutex, so all public methods are safe to call from any thread.
 */
class ProbesManager
{
public:
    ProbesManager();

    /**
     * Start with an already-open NFC device and a custom scanner for (re-)enumeration (tests,
     * custom transports). An empty @p scanner finds nothing, so rescans keep the current device.
     */
    explicit ProbesManager(std::shared_ptr<NfcProbeDevice> nfcDevice,
                           NfcProbeScanner scanner = nullptr);

    /**
     * Scan for probes and adopt the NFC probe found, if any.
     * @p callback runs under the probe lock and must not call back into ProbesManager.
     */
    auto enumerate(probe_t probesSet, const std::string pbspk2commPath = {},
                   const ProbeDisplayCallback &callback = nullptr) -> void;

    auto hasNfc() const -> bool;

    /** What the NFC probe was opened on (NfcProbeDevice::devicePath); empty if none or flashing. */
    auto nfcDevicePath() const -> std::string;

    /**
     * Scan for the NFC probe again, adopting one found (e.g. attached or replaced after start).
     * If none is found the current device is kept. A tap read during the scan is queued for
     * @ref isNfcTagRead. Blocks for the duration of the USB scan.
     */
    auto rescanNfc() -> void;

    /**
     * True once per NFC tap, including taps observed (and cleared) by @ref nfcProbeInfo.
     * Never blocks: returns false without polling while another call holds the probe (e.g. a
     * rescan); queued taps and the pad's own tap are reported by a later poll.
     */
    auto isNfcTagRead() -> bool;
    auto setNfcTag(const std::string &tag) -> bool;

    /**
     * Never blocks: applies @p mode now if the probe is free, else the next @ref isNfcTagRead
     * poll applies it (the latest mode wins; a failed write is retried on later polls).
     * @return true if applied or queued (always true while the probe is busy, before presence is
     * known), false if @p mode is invalid or no NFC probe is present.
     */
    auto setNfcLeds(NfcLedMode mode) -> bool;

    auto setDiscoveryDescription(uint64_t providerSerial, const std::string &gameName,
                                 const std::string &providerInfo, const std::string &extraInfo)
            -> bool;

    /// @p identity, if given, receives the probe's identity when the read behind the reason
    /// succeeds, so a caller needs no second read.
    auto probesBootReason(ProbeType probeType, std::optional<ProbeInformation> *identity = nullptr)
            -> std::optional<std::string>;

    /**
     * Read the NFC probe's identity/status and NFC state. A tap seen by this read is kept
     * pending and reported by the next @ref isNfcTagRead, so this never consumes a tap the SDK
     * would otherwise have seen.
     */
    auto nfcProbeInfo() -> NfcProbeInfoResult;

    class FlashHold;

    /**
     * Holds the NFC probe for a firmware flash (SB-5759). Until the hold is destroyed every other
     * method returns at once without touching the probe, so no poll waits on a flash. Destroying
     * it after @ref FlashHold::enterBootloader forgets the probe, which re-enumerates; call
     * @ref rescanNfc to find it again. Null when no NFC probe is present.
     */
    auto holdNfcForFlash() -> std::unique_ptr<FlashHold>;

private:
    // forceRescan: treat as a rescan (queue a tap read here) even if no probe was known yet
    auto enumerateLocked(probe_t probesSet, const ProbeDisplayCallback &callback,
                         bool forceRescan = false) -> void;
    auto recordSeenTapLocked(uint32_t tapId) -> void;
    auto applyLedModeLocked() -> void;

    static constexpr int NO_LED_MODE {-1};

    mutable std::mutex m_mutex;
    // Set by FlashHold before it takes m_mutex, so callers return instead of queueing behind it
    std::atomic<bool> m_flashing {false};
    // LED mode the host asked for (set without the lock) and the one last written to the probe
    std::atomic<int> m_desiredLedMode {NO_LED_MODE};
    int m_appliedLedMode {NO_LED_MODE};
    NfcProbeScanner m_scanner;
    std::shared_ptr<NfcProbeDevice> m_nfc;
    // Taps read (and cleared) outside isNfcTagRead() but not yet reported by it
    uint32_t m_pendingTaps {0};
    // Last non-zero tap id seen by any read (poll, info, enumeration) and when
    uint32_t m_lastSeenTapId {0};
    uint64_t m_lastSeenTapUnixMs {0};
    std::shared_ptr<NetworkDiscovery> m_discovery;
    NetworkDiscovery::DeviceInfo_t m_discoveryInfo;
};

class ProbesManager::FlashHold
{
public:
    ~FlashHold();
    FlashHold(const FlashHold &) = delete;
    FlashHold &operator=(const FlashHold &) = delete;

    /** See NfcProbeDevice::devicePath. */
    auto devicePath() const -> std::string;
    /** See NfcProbeDevice::enterBootloader. */
    auto enterBootloader() -> void;

private:
    friend class ProbesManager;
    explicit FlashHold(ProbesManager &manager);

    ProbesManager &m_manager;
    std::unique_lock<std::mutex> m_lock;
    bool m_bootloaderSent {false};
};

} // namespace nfc
