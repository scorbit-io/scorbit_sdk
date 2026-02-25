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

#include "soft_key_resolver.h"
#include "device_info.h"
#include "provisioning_client.h"
#include "net_util.h"
#include "utils/decrypt.h"
#include "utils/signer.h"
#include <tpm/crypto_helpers.h>
#include <logger/logger.h>
#include <utils/bytearray.h>
#include <nlohmann/json.hpp>
#include <obfuscate.h>
#include <openssl/sha.h>
#include <chrono>

using json = nlohmann::json;

namespace scorbit {
namespace detail {

namespace {

constexpr int ECDSA_P256_KEY_SIZE = 32;
// constexpr int ECDSA_P256_PUBKEY_SIZE = 65;

} // namespace

bool SoftKeyResolver::tryResolve(DeviceInfo &info)
{
    if (!info.hasSoftKeyProvisioning()) {
        return false;
    }

    if (tryLoadKey(info)) {
        return true;
    }

    return provisionNewKey(info);
}

SignerCallback SoftKeyResolver::createSigner() const
{
    auto key = m_devicePrivateKey;
    return [key](const Digest &digest) -> Signature {
        Signature signature;
        auto result = Signer::sign(signature, digest, key);
        if (result != SignErrorCode::Ok) {
            ERR("SoftKeyResolver: signing failed, error {}", static_cast<int>(result));
            signature.clear();
        }
        return signature;
    };
}

bool SoftKeyResolver::tryLoadKey(DeviceInfo &info)
{
    if (!info.loadKeyCallback) {
        return false;
    }

    const auto keyData = info.loadKeyCallback();
    if (keyData.empty()) {
        INF("SoftKeyResolver: no saved key found");
        return false;
    }

    try {
        auto j = json::parse(keyData);
        const auto privateKeyHex = j.at("private_key").get<std::string>();
        const auto uuid = j.at("uuid").get<std::string>();
        const auto serialNumber = j.at("serial_number").get<uint64_t>();

        utils::ByteArray privateKey(privateKeyHex);
        if (privateKey.size() != ECDSA_P256_KEY_SIZE) {
            ERR("SoftKeyResolver: invalid key size {}, expected {}", privateKey.size(),
                ECDSA_P256_KEY_SIZE);
            return false;
        }

        m_devicePrivateKey.assign(privateKey.begin(), privateKey.end());
        info.uuid = uuid;
        info.serialNumber = serialNumber;

        INF("SoftKeyResolver: loaded saved key, uuid={}", uuid);
        return true;
    } catch (const std::exception &e) {
        ERR("SoftKeyResolver: failed to parse saved key: {}", e.what());
        return false;
    }
}

bool SoftKeyResolver::provisionNewKey(DeviceInfo &info)
{
    // Decrypt the provider's private key
    const auto providerKey =
            decryptSecret(info.encryptedKey,
                          info.provider + std::string(AY_OBFUSCATE(SCORBIT_SDK_ENCRYPT_SECRET)));
    if (providerKey.empty()) {
        ERR("SoftKeyResolver: failed to decrypt provider key");
        return false;
    }

    // Generate a unique device ECDSA key pair
    utils::ByteArray publicKey;
    utils::ByteArray privateKey;
    if (!generateEcdsaKeyPair(publicKey, privateKey)) {
        ERR("SoftKeyResolver: failed to generate key pair");
        return false;
    }

    ProvisioningClient client(info.hostname);

    // Step 1: Initiate provisioning (GET) to receive uuid and serial
    auto result = client.initiate(info.provider, providerKey);
    if (!result) {
        ERR("SoftKeyResolver: provisioning initiation failed");
        return false;
    }

    // Step 2: Create device signature over uuid_bytes + timestamp
    const auto timestamp =
            std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count());

    utils::ByteArray uuidBytes(removeSymbols(result->uuid, "-{}"));
    utils::ByteArray timestampBytes(timestamp.cbegin(), timestamp.cend());
    uuidBytes.insert(uuidBytes.end(), timestampBytes.cbegin(), timestampBytes.cend());

    std::array<uint8_t, SHA256_DIGEST_LENGTH> digest {};
    SHA256(uuidBytes.data(), uuidBytes.size(), digest.data());

    utils::ByteArray digestArray(digest.data(), digest.size());
    utils::ByteArray deviceSignature;
    if (!ecdsaSign(privateKey, digestArray, deviceSignature)) {
        ERR("SoftKeyResolver: failed to sign provisioning confirmation");
        return false;
    }

    // Step 3: Confirm provisioning (POST)
    if (!client.confirm(*result, publicKey.hex(), deviceSignature.hex(), timestamp, info.provider,
                        providerKey)) {
        ERR("SoftKeyResolver: provisioning confirmation failed");
        return false;
    }

    // Persist the device key via callback
    m_devicePrivateKey.assign(privateKey.begin(), privateKey.end());
    info.uuid = result->uuid;
    info.serialNumber = result->serialNumber;

    if (info.saveKeyCallback) {
        json keyJson;
        keyJson["private_key"] = privateKey.hex();
        keyJson["uuid"] = result->uuid;
        keyJson["serial_number"] = result->serialNumber;
        info.saveKeyCallback(keyJson.dump());
    }

    INF("SoftKeyResolver: provisioned new device, uuid={}", result->uuid);
    return true;
}

} // namespace detail
} // namespace scorbit
