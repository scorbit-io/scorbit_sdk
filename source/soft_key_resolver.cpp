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
#include "utils/machine_fingerprint.h"
#include <tpm/crypto_helpers.h>
#include <logger/logger.h>
#include <utils/bytearray.h>
#include <nlohmann/json.hpp>
#include <obfuscate.h>
#include <openssl/crypto.h>
#include <openssl/sha.h>
#include <cstring>

using json = nlohmann::json;

namespace scorbit {
namespace detail {

namespace {

constexpr int ECDSA_P256_KEY_SIZE = 32;

std::string buildHmacMessage(const std::string &provider, const std::string &uuid,
                             uint64_t serialNumber, const std::string &encryptedKey)
{
    return provider + uuid + std::to_string(serialNumber) + encryptedKey;
}

} // namespace

bool SoftKeyResolver::tryResolve(DeviceInfo &info, const std::string &serverTimestamp)
{
    if (!info.hasSoftKeyProvisioning()) {
        return false;
    }

    auto providerKey =
            decryptSecret(info.encryptedKey,
                          info.provider + std::string(AY_OBFUSCATE(SCORBIT_SDK_ENCRYPT_SECRET)));
    if (providerKey.empty()) {
        ERR("SoftKeyResolver: failed to decrypt provider key");
        return false;
    }

    utils::ByteArray providerKeyBa(providerKey.data(), providerKey.size());
    m_deviceKeyPassword = providerKeyBa.hex();

    bool success = tryLoadKey(info) || provisionNewKey(info, providerKey, serverTimestamp);

    OPENSSL_cleanse(providerKey.data(), providerKey.size());

    if (!success) {
        OPENSSL_cleanse(m_deviceKeyPassword.data(), m_deviceKeyPassword.size());
        m_deviceKeyPassword.clear();
    }

    return success;
}

SignerCallback SoftKeyResolver::createSigner() const
{
    auto encDevKey = m_encryptedDeviceKey;
    auto password = m_deviceKeyPassword;

    return [encDevKey, password](const Digest &digest) -> Signature {
        auto decrypted = decryptSecret(encDevKey, password);
        if (decrypted.empty()) {
            ERR("SoftKeyResolver: failed to decrypt device key for signing");
            return {};
        }

        utils::ByteArray key(decrypted.data(), decrypted.size());
        utils::ByteArray digestBa(digest.data(), digest.size());
        utils::ByteArray sig;
        bool ok = ecdsaSign(key, digestBa, sig);

        OPENSSL_cleanse(decrypted.data(), decrypted.size());
        decrypted.clear();

        if (!ok) {
            ERR("SoftKeyResolver: signing failed");
            return {};
        }
        return Signature(sig.begin(), sig.end());
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
        const auto encryptedKey = j.at("encrypted_key").get<std::string>();
        const auto uuid = j.at("uuid").get<std::string>();
        const auto serialNumber = j.at("serial_number").get<uint64_t>();
        const auto provider = j.at("provider").get<std::string>();
        const auto storedHmac = j.at("hmac").get<std::string>();

        if (provider != info.provider) {
            ERR("SoftKeyResolver: provider mismatch (stored='{}', expected='{}')", provider,
                info.provider);
            return false;
        }

        const auto expectedHmac = computeHmac(
                buildHmacMessage(provider, uuid, serialNumber, encryptedKey), m_deviceKeyPassword);

        if (storedHmac.size() != expectedHmac.size()
            || CRYPTO_memcmp(storedHmac.data(), expectedHmac.data(), expectedHmac.size()) != 0) {
            ERR("SoftKeyResolver: HMAC verification failed -- key data may have been tampered "
                "with");
            return false;
        }

        auto decrypted = decryptSecret(encryptedKey, m_deviceKeyPassword);
        if (decrypted.empty()) {
            ERR("SoftKeyResolver: failed to decrypt stored device key");
            return false;
        }

        if (decrypted.size() != ECDSA_P256_KEY_SIZE) {
            ERR("SoftKeyResolver: invalid key size {}, expected {}", decrypted.size(),
                ECDSA_P256_KEY_SIZE);
            OPENSSL_cleanse(decrypted.data(), decrypted.size());
            return false;
        }

        OPENSSL_cleanse(decrypted.data(), decrypted.size());

        m_encryptedDeviceKey = encryptedKey;
        info.uuid = uuid;
        info.serialNumber = serialNumber;

        INF("SoftKeyResolver: loaded saved key, uuid={}", uuid);
        return true;
    } catch (const std::exception &e) {
        ERR("SoftKeyResolver: failed to parse saved key: {}", e.what());
        return false;
    }
}

bool SoftKeyResolver::provisionNewKey(DeviceInfo &info, const std::vector<uint8_t> &providerKey,
                                      const std::string &serverTimestamp)
{
    utils::ByteArray publicKey;
    utils::ByteArray privateKey;
    if (!generateEcdsaKeyPair(publicKey, privateKey)) {
        ERR("SoftKeyResolver: failed to generate key pair");
        return false;
    }

    ProvisioningClient client(info.hostname, makeSslOptions());

    auto result = client.initiate(info.provider, providerKey, serverTimestamp);
    if (!result) {
        ERR("SoftKeyResolver: provisioning initiation failed");
        return false;
    }

    utils::ByteArray uuidBytes(removeSymbols(result->uuid, "-{}"));
    utils::ByteArray timestampBytes(serverTimestamp.cbegin(), serverTimestamp.cend());
    uuidBytes.insert(uuidBytes.end(), timestampBytes.cbegin(), timestampBytes.cend());

    std::array<uint8_t, SHA256_DIGEST_LENGTH> digest {};
    SHA256(uuidBytes.data(), uuidBytes.size(), digest.data());

    utils::ByteArray digestArray(digest.data(), digest.size());
    utils::ByteArray deviceSignature;
    if (!ecdsaSign(privateKey, digestArray, deviceSignature)) {
        ERR("SoftKeyResolver: failed to sign provisioning confirmation");
        return false;
    }

    auto fingerprints = collectFingerprints();
    const auto fpHash = fingerprints.computeHash();
    INF("SoftKeyResolver: fingerprint hash={}", fpHash);

    // Strip the 0x04 uncompressed point prefix — API expects raw 64-byte (x || y)
    utils::ByteArray rawPublicKey(publicKey.data() + 1, publicKey.size() - 1);

    auto confirmed = client.confirm(*result, rawPublicKey.hex(), deviceSignature.hex(),
                                    serverTimestamp, info.provider, providerKey, fpHash);
    if (!confirmed) {
        ERR("SoftKeyResolver: provisioning confirmation failed");
        return false;
    }

    std::vector<uint8_t> rawKey(privateKey.begin(), privateKey.end());
    const auto encDevKey = encryptSecret(rawKey, m_deviceKeyPassword);
    OPENSSL_cleanse(rawKey.data(), rawKey.size());

    info.uuid = confirmed->uuid;
    info.serialNumber = confirmed->serialNumber;
    m_encryptedDeviceKey = encDevKey;

    if (info.saveKeyCallback) {
        const auto hmac = computeHmac(buildHmacMessage(info.provider, confirmed->uuid,
                                                       confirmed->serialNumber, encDevKey),
                                      m_deviceKeyPassword);

        json keyJson;
        keyJson["encrypted_key"] = encDevKey;
        keyJson["uuid"] = confirmed->uuid;
        keyJson["serial_number"] = confirmed->serialNumber;
        keyJson["provider"] = info.provider;
        keyJson["hmac"] = hmac;
        info.saveKeyCallback(keyJson.dump());
    }

    INF("SoftKeyResolver: provisioned new device, uuid={}", confirmed->uuid);
    return true;
}

} // namespace detail
} // namespace scorbit
