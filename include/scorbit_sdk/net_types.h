/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include "net_types_c.h"
#include <functional>
#include <string>
#include <vector>

namespace scorbit {

constexpr auto DIGEST_LENGTH = SB_DIGEST_LENGTH;
constexpr auto UUID_LENGTH = SB_UUID_LENGTH;
constexpr auto SIGNATURE_MAX_LENGTH = SB_SIGNATURE_MAX_LENGTH;
constexpr auto KEY_LENGTH = SB_KEY_LENGTH;

// IMPORTANT: always add new error codes at the end of the list and sync with sb_error_t
enum class Error {
    Success = SB_EC_SUCCESS,        // Success
    Unknown = SB_EC_UNKNOWN,        // Unknown error
    AuthFailed = SB_EC_AUTH_FAILED, // Authentication failed
    NotPaired = SB_EC_NOT_PAIRED,   // Device is not paired
    ApiError = SB_EC_API_ERROR,     // API call error (e.g., HTTP error code != 200)
    FileError = SB_EC_FILE_ERROR,   // File error (e.g., file not found)
};

enum class AuthStatus {
    NotAuthenticated = SB_NET_NOT_AUTHENTICATED,
    Authenticating = SB_NET_AUTHENTICATING,
    AuthenticatedCheckingPairing = SB_NET_AUTHENTICATED_CHECKING_PAIRING,
    AuthenticatedUnpaired = SB_NET_AUTHENTICATED_UNPAIRED,
    AuthenticatedPaired = SB_NET_AUTHENTICATED_PAIRED,
    AuthenticationFailed = SB_NET_AUTHENTICATION_FAILED,
};

struct DeviceInfo {
    /** Mandatory. The provider name, e.g., "scorbitron", "vpin". */
    std::string provider;

    /** Mandatory for manufacturers. The Machine ID assigned by Scorbit, ex. 4419. */
    int32_t machineId {0};

    /** Mandatory. The game code version, e.g., "1.12.3". */
    std::string gameCodeVersion;

    /**
     * Optional. The hostname of the server to connect to.
     * If not set, the default `"production"` hostname will be used. The two standard options
     * are `"production"` and `"staging"`, each mapped to pre-defined URLs. To use a custom URL,
     * provide it in the format `"http[s]://<url>[:port]"`.
     *
     * Examples: `"production"`, `"staging"`, `"https://api.scorbit.io"`,
     * `"https://staging.scorbit.io"`, `"http://localhost:8080"`
     */
    std::string hostname;

    /**
     * Optional. The device's UUID.
     * If not set, the UUID will be derived from the device's MAC address. This is the preferred way
     * to set the UUID for machines without TPM. If you have a known UUID, you can set it here.
     *
     * Examples: `"f0b188f8-9f2d-4f8d-abe4-c3107516e7ce"`, `"f0b188f89f2d4f8dabe4c3107516e7ce"`,
     * `"F0B188F8-9F2D-4F8D-ABE4-C3107516E7CE"`, `"F0B188F89F2D4F8DABE4C3107516E7CE"`
     */
    std::string uuid;

    /** Optional. The serial number of the device. Set to 0 if unavailable. */
    uint64_t serialNumber {0};

    /** If true, the SDK will automatically download players' profile pictures */
    bool autoDownloadPlayerPics {false};

    /**
     * Optional. An array of score features that help identify what triggered a score increase
     * (e.g., ramp, spinner, target, etc.).
     * Leave this vector empty if no specific features are provided.
     *
     * Example: info.scoreFeatures = {"ramp", "left spinner", "right spinner"}
     */
    std::vector<std::string> scoreFeatures;

    /**
     * Optional. Version number for the score features.
     * Set to 1 if score features are used. Increment this version when new features are added
     * in future game releases.
     * Ignored if @ref scoreFeatures is empty.
     */
    int scoreFeaturesVersion {0};


    // ------------ FOR INTERNAL USE ---------------------------------------------------------

    // Helper methods for conversion DeviceInfo <-> sb_device_info_t
    DeviceInfo() = default;

    DeviceInfo(const sb_device_info_t &di)
        : provider {di.provider}
        , machineId {di.machine_id}
        , gameCodeVersion {di.game_code_version ? di.game_code_version : std::string {}}
        , hostname {di.hostname ? di.hostname : std::string {}}
        , uuid {di.uuid ? di.uuid : std::string {}}
        , serialNumber {di.serial_number}
        , autoDownloadPlayerPics {di.auto_download_player_pics}
        , scoreFeaturesVersion {di.score_features_version}
    {
        if (di.score_features && di.score_features_count > 0) {
            scoreFeatures.reserve(di.score_features_count);
            for (size_t i = 0; i < di.score_features_count; ++i) {
                scoreFeatures.emplace_back(di.score_features[i] ? di.score_features[i]
                                                                : std::string {});
            }
        }
    }

    operator sb_device_info_t() const
    {
        sb_device_info_t di;
        di.provider = provider.c_str();
        di.machine_id = machineId;
        di.game_code_version = gameCodeVersion.c_str();
        di.hostname = hostname.c_str();
        di.uuid = uuid.c_str();
        di.serial_number = serialNumber;
        di.auto_download_player_pics = autoDownloadPlayerPics;
        di.score_features_version = scoreFeaturesVersion;

        // Temporary array of C-string pointers (valid as long as `*this` lives)
        if (!scoreFeatures.empty()) {
            tempTagPtrs.clear();
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
    // Used internally to hold C-string pointers for sb_device_info_t
    mutable std::vector<const char *> tempTagPtrs;
};

using StringCallback = std::function<void(Error error, const std::string &reply)>;
using VectorCallback = std::function<void(Error error, const std::vector<uint8_t> &data)>;

} // namespace scorbit
