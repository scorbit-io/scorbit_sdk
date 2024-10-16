/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Oct 2024
 *
 ****************************************************************************/

#pragma once

#include "net_types_c.h"
#include <array>
#include <functional>
#include <string>
#include <cstddef>

namespace scorbit {

constexpr auto DIGEST_LENGTH = SB_DIGEST_LENGTH;
constexpr auto UUID_LENGTH = SB_UUID_LENGTH;
constexpr auto SIGNATURE_MAX_LENGTH = SB_SIGNATURE_MAX_LENGTH;
constexpr auto KEY_LENGTH = SB_KEY_LENGTH;

struct DeviceInfo {
    /** Required. The provider name, e.g., "scorbitron", "vpin". */
    std::string provider;

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
     * If not set, the UUID will be derived from the device's MAC address.
     *
     * Examples: `"f0b188f8-9f2d-4f8d-abe4-c3107516e7ce"`, `"f0b188f89f2d4f8dabe4c3107516e7ce"`,
     * `"F0B188F8-9F2D-4F8D-ABE4-C3107516E7CE"`, `"F0B188F89F2D4F8DABE4C3107516E7CE"`
     */
    std::string uuid;

    /** Optional. The serial number of the device. Set to 0 if unavailable. */
    uint64_t serialNumber {0};
};

using Signature = std::array<uint8_t, SIGNATURE_MAX_LENGTH>;
using Digest = std::array<uint8_t, DIGEST_LENGTH>;
using Key = std::array<uint8_t, KEY_LENGTH>;

using SignerCallback =
        std::function<bool(Signature &signature, size_t &signatureLen, const Digest &digest)>;

} // namespace scorbit
