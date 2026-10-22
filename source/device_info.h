/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
 *
 * MIT License
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <scorbit_sdk/config_c.h>
#include <scorbit_sdk/event.h>
#include <scorbit_sdk/net_types.h>
#include "event_classes.h"
#include "identifiers.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace scorbit {

/// A probe's installed firmware: its build number (0 when the firmware reports none) and UID,
/// and how it last booted. Legacy firmware reports no boot info: boot reason 0, count 0.
struct ProbeFirmware {
    std::uint64_t timestampUtc {0};
    std::uint32_t uid {0};
    std::uint8_t bootReason {0}; ///< The firmware's BootReason byte; 0 is unknown.
    std::uint32_t watchdogCount {0};

    bool operator==(const ProbeFirmware &other) const
    {
        return timestampUtc == other.timestampUtc && uid == other.uid
            && bootReason == other.bootReason && watchdogCount == other.watchdogCount;
    }
};

/// The probes a host reports itself. The SDK reads the NFC pad, so a host can't report it.
inline bool isHostReportedProbe(std::string_view probe)
{
    return probe == detail::JVAL_PROBE_CPU || probe == detail::JVAL_PROBE_DMD;
}

/**
 * Internal DeviceInfo structure.
 *
 * This is the internal representation of device/game configuration used by the SDK.
 * It is accessed via the opaque sb_config_t handle in C or the Config class in C++.
 *
 * This structure is opaque to external API consumers, allowing new fields to be added
 * in future versions without breaking ABI.
 */
struct DeviceInfo {
    std::string provider;
    int32_t machineId {0};
    std::string gameCodeVersion;
    std::string hostname;
    std::string cfHostname;
    /// Heartbeat v2 UDP endpoint overrides. Empty host / zero port mean "use the built-in
    /// default", so an integrator that never calls the setters keeps production behaviour.
    std::string heartbeatHost;
    uint16_t heartbeatPort {0};
    std::string uuid;
    uint64_t serialNumber {0};
    bool autoDownloadPlayerPics {false};
    std::vector<std::string> scoreFeatures;
    int scoreFeaturesVersion {0};

    /// Per-thread nice for SDK worker threads (Linux SCHED_BATCH + setpriority); 0 = do not adjust.
    int threadsNice {0};

    static constexpr int MIN_WORKER_THREADS = 1;
    static constexpr int MAX_WORKER_THREADS = 8;
    static constexpr int DEFAULT_WORKER_THREADS = 4;

    /// Threads for blocking work (HTTP, crypto, downloads, archive extraction). Timers and the
    /// realtime connection run on their own threads and are not affected by this.
    int workerThreadCount {DEFAULT_WORKER_THREADS};

    // Authentication - one of these must be set
    std::string encryptedKey;
    sb_signer_callback_t signerCallback {nullptr};
    void *signerUserData {nullptr};

    // Internal use only
    std::string scorbitdVersion;
    std::string scorbitdPlatformId;
    // The game's firmware as scorbitd reads it from its probe; empty when unknown. Config only,
    // never in sb_device_info_t, whose layout is ABI.
    std::string gameFirmwareId;
    std::string gameFirmwareVersion;
    std::string gameSerial;
    std::string pbspkcommFirmwareId; ///< The game firmware ID the installed pbspk*comm targets.
    /// Probes the host reported (CPU, DMD) by name; nullopt marks one that is not attached.
    std::map<std::string, std::optional<ProbeFirmware>> probeFirmware;
    std::string machineTitle;
    std::string extraFingerprint;

    // Event callback - stored here and passed to EventManager
    detail::EventCallback m_eventCallback;

    // Key persistence callbacks - stored as std::function (similar to m_eventCallback)
    SaveKeyCallback saveKeyCallback;
    LoadKeyCallback loadKeyCallback;

    DeviceInfo() = default;

    /**
     * Construct from C sb_device_info_t (for deprecated API compatibility).
     */
    explicit DeviceInfo(const sb_device_info_t &di)
        : provider {di.provider ? di.provider : ""}
        , machineId {di.machine_id}
        , gameCodeVersion {di.game_code_version ? di.game_code_version : ""}
        , hostname {di.hostname ? di.hostname : ""}
        , uuid {di.uuid ? di.uuid : ""}
        , serialNumber {di.serial_number}
        , autoDownloadPlayerPics {di.auto_download_player_pics}
        , scoreFeaturesVersion {di.score_features_version}
        , scorbitdVersion {di.scorbitd_version ? di.scorbitd_version : ""}
        , scorbitdPlatformId {di.scorbitd_platform_id ? di.scorbitd_platform_id : ""}
    {
        if (di.score_features && di.score_features_count > 0) {
            scoreFeatures.reserve(di.score_features_count);
            for (size_t i = 0; i < di.score_features_count; ++i) {
                scoreFeatures.emplace_back(di.score_features[i] ? di.score_features[i] : "");
            }
        }
    }

    /**
     * Check if authentication is configured (either encrypted key or signer).
     */
    bool hasAuthentication() const { return !encryptedKey.empty() || signerCallback != nullptr; }

    /**
     * Check if using encrypted key authentication.
     */
    bool usesEncryptedKey() const { return !encryptedKey.empty(); }

    bool hasAuthenticationCallback() const { return signerCallback != nullptr; }

    /**
     * Check if software key provisioning is possible (encrypted key + both callbacks).
     */
    bool hasSoftKeyProvisioning() const
    {
        return !encryptedKey.empty() && saveKeyCallback && loadKeyCallback;
    }

    /**
     * Convert to C sb_device_info_t (for deprecated API compatibility and testing).
     * Note: The returned struct contains pointers to this object's strings,
     * so it's only valid as long as this DeviceInfo exists.
     */
    operator sb_device_info_t() const
    {
        sb_device_info_t di {};
        di.provider = provider.c_str();
        di.machine_id = machineId;
        di.game_code_version = gameCodeVersion.c_str();
        di.hostname = hostname.c_str();
        di.uuid = uuid.c_str();
        di.serial_number = serialNumber;
        di.auto_download_player_pics = autoDownloadPlayerPics;
        di.score_features_version = scoreFeaturesVersion;
        di.scorbitd_version = scorbitdVersion.c_str();
        di.scorbitd_platform_id = scorbitdPlatformId.c_str();

        if (!scoreFeatures.empty()) {
            tempTagPtrs.clear();
            tempTagPtrs.reserve(scoreFeatures.size());
            for (const auto &tag : scoreFeatures) {
                tempTagPtrs.push_back(tag.c_str());
            }
            di.score_features = tempTagPtrs.data();
            di.score_features_count = tempTagPtrs.size();
        } else {
            di.score_features = nullptr;
            di.score_features_count = 0;
        }

        return di;
    }

private:
    // Used internally to hold C-string pointers for sb_device_info_t conversion
    mutable std::vector<const char *> tempTagPtrs;
};

} // namespace scorbit

// C API compatibility - sb_config_t is a pointer to this
struct sb_config_s : public scorbit::DeviceInfo {
};
