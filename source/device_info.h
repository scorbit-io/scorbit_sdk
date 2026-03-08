/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
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
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace scorbit {

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
    std::string uuid;
    uint64_t serialNumber {0};
    bool autoDownloadPlayerPics {false};
    std::vector<std::string> scoreFeatures;
    int scoreFeaturesVersion {0};

    // Authentication - one of these must be set
    std::string encryptedKey;
    sb_signer_callback_t signerCallback {nullptr};
    void *signerUserData {nullptr};

    // Internal use only
    std::string scorbitdVersion;
    std::string scorbitdPlatformId;
    std::string machineTitle;

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
