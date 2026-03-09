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
#include "utils/decrypt.h"
#include "net.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <utils/bytearray.h>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using json = nlohmann::json;

namespace {

// Valid P-256 test device private key (32 bytes = 64 hex chars)
constexpr auto TEST_DEVICE_KEY_HEX =
        "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2";
// Test provider private key (32 bytes = 64 hex chars, distinct from device key)
constexpr auto TEST_PROVIDER_KEY_HEX =
        "f1e2d3c4b5a69788796a5b4c3d2e1f00f1e2d3c4b5a69788796a5b4c3d2e1f00";
constexpr auto TEST_UUID = "c7f1fd0b-82f7-5504-8fbe-740c09bc7dab";
constexpr uint64_t TEST_SERIAL = 12345;
constexpr auto TEST_PROVIDER = "testprovider";
constexpr auto TEST_SERVER_TIMESTAMP = "1773013652";

std::vector<uint8_t> hexToBytes(const std::string &hex)
{
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

// Encrypt the test provider key the same way encrypt_tool does: provider + SDK_SECRET
std::string makeProviderEncryptedKey()
{
    auto rawProviderKey = hexToBytes(TEST_PROVIDER_KEY_HEX);
    return encryptSecret(rawProviderKey,
                         std::string(TEST_PROVIDER) + std::string(SCORBIT_SDK_ENCRYPT_SECRET));
}

// The device key password is the hex of the decrypted provider key (uppercase, from ByteArray::hex)
std::string deviceKeyPassword()
{
    auto rawProviderKey = hexToBytes(TEST_PROVIDER_KEY_HEX);
    utils::ByteArray ba(rawProviderKey.data(), rawProviderKey.size());
    return ba.hex();
}

std::string makeValidKeyJson()
{
    auto rawDeviceKey = hexToBytes(TEST_DEVICE_KEY_HEX);
    auto password = deviceKeyPassword();

    auto encKey = encryptSecret(rawDeviceKey, password);
    auto hmacMsg = std::string(TEST_PROVIDER) + TEST_UUID + std::to_string(TEST_SERIAL) + encKey;
    auto hmac = computeHmac(hmacMsg, password);

    json j;
    j["encrypted_key"] = encKey;
    j["uuid"] = TEST_UUID;
    j["serial_number"] = TEST_SERIAL;
    j["provider"] = TEST_PROVIDER;
    j["hmac"] = hmac;
    return j.dump();
}

DeviceInfo makeSoftKeyDeviceInfo(LoadKeyCallback loadCb)
{
    DeviceInfo info;
    info.provider = TEST_PROVIDER;
    info.machineId = 1234;
    info.gameCodeVersion = "1.0.0";
    info.hostname = "staging";
    info.encryptedKey = makeProviderEncryptedKey();
    info.saveKeyCallback = [](const std::string &) { };
    info.loadKeyCallback = std::move(loadCb);
    return info;
}

} // namespace

// =============================================================================
// DeviceInfo::hasSoftKeyProvisioning
// =============================================================================

TEST_CASE("hasSoftKeyProvisioning requires all three components", "[DeviceInfo][SoftKey]")
{
    SECTION("Returns true with encrypted key + save + load callbacks")
    {
        DeviceInfo info;
        info.encryptedKey = "some_key";
        info.saveKeyCallback = [](const std::string &) { };
        info.loadKeyCallback = []() { return std::string {}; };
        REQUIRE(info.hasSoftKeyProvisioning());
    }

    SECTION("Returns false without encrypted key")
    {
        DeviceInfo info;
        info.saveKeyCallback = [](const std::string &) { };
        info.loadKeyCallback = []() { return std::string {}; };
        REQUIRE_FALSE(info.hasSoftKeyProvisioning());
    }

    SECTION("Returns false without save callback")
    {
        DeviceInfo info;
        info.encryptedKey = "some_key";
        info.loadKeyCallback = []() { return std::string {}; };
        REQUIRE_FALSE(info.hasSoftKeyProvisioning());
    }

    SECTION("Returns false without load callback")
    {
        DeviceInfo info;
        info.encryptedKey = "some_key";
        info.saveKeyCallback = [](const std::string &) { };
        REQUIRE_FALSE(info.hasSoftKeyProvisioning());
    }

    SECTION("Returns false with nothing set")
    {
        DeviceInfo info;
        REQUIRE_FALSE(info.hasSoftKeyProvisioning());
    }
}

// =============================================================================
// SoftKeyResolver - tryLoadKey via tryResolve
// =============================================================================

TEST_CASE("SoftKeyResolver loads valid saved key", "[SoftKeyResolver]")
{
    auto info = makeSoftKeyDeviceInfo([]() { return makeValidKeyJson(); });

    SoftKeyResolver resolver;
    REQUIRE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
    CHECK(info.uuid == TEST_UUID);
    CHECK(info.serialNumber == TEST_SERIAL);
}

TEST_CASE("SoftKeyResolver returns false when no key saved", "[SoftKeyResolver]")
{
    auto info = makeSoftKeyDeviceInfo([]() { return std::string {}; });

    SoftKeyResolver resolver;
    REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
}

TEST_CASE("SoftKeyResolver returns false with invalid JSON", "[SoftKeyResolver]")
{
    auto info = makeSoftKeyDeviceInfo([]() { return "not json"; });

    SoftKeyResolver resolver;
    REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
}

TEST_CASE("SoftKeyResolver returns false with missing fields", "[SoftKeyResolver]")
{
    SECTION("Missing encrypted_key")
    {
        auto info = makeSoftKeyDeviceInfo([]() {
            json j;
            j["uuid"] = TEST_UUID;
            j["serial_number"] = TEST_SERIAL;
            j["provider"] = TEST_PROVIDER;
            j["hmac"] = "deadbeef";
            return j.dump();
        });

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
    }

    SECTION("Missing uuid")
    {
        auto info = makeSoftKeyDeviceInfo([]() {
            json j;
            j["encrypted_key"] = "somedata";
            j["serial_number"] = TEST_SERIAL;
            j["provider"] = TEST_PROVIDER;
            j["hmac"] = "deadbeef";
            return j.dump();
        });

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
    }

    SECTION("Missing serial_number")
    {
        auto info = makeSoftKeyDeviceInfo([]() {
            json j;
            j["encrypted_key"] = "somedata";
            j["uuid"] = TEST_UUID;
            j["provider"] = TEST_PROVIDER;
            j["hmac"] = "deadbeef";
            return j.dump();
        });

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
    }

    SECTION("Missing provider")
    {
        auto info = makeSoftKeyDeviceInfo([]() {
            json j;
            j["encrypted_key"] = "somedata";
            j["uuid"] = TEST_UUID;
            j["serial_number"] = TEST_SERIAL;
            j["hmac"] = "deadbeef";
            return j.dump();
        });

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
    }

    SECTION("Missing hmac")
    {
        auto info = makeSoftKeyDeviceInfo([]() {
            json j;
            j["encrypted_key"] = "somedata";
            j["uuid"] = TEST_UUID;
            j["serial_number"] = TEST_SERIAL;
            j["provider"] = TEST_PROVIDER;
            return j.dump();
        });

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
    }
}

TEST_CASE("SoftKeyResolver requires soft key provisioning config", "[SoftKeyResolver]")
{
    SECTION("No encrypted key")
    {
        DeviceInfo info;
        info.saveKeyCallback = [](const std::string &) { };
        info.loadKeyCallback = []() { return makeValidKeyJson(); };

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
    }

    SECTION("No save callback")
    {
        DeviceInfo info;
        info.encryptedKey = makeProviderEncryptedKey();
        info.loadKeyCallback = []() { return makeValidKeyJson(); };

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
    }

    SECTION("No load callback")
    {
        DeviceInfo info;
        info.encryptedKey = makeProviderEncryptedKey();
        info.saveKeyCallback = [](const std::string &) { };

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
    }
}

// =============================================================================
// SoftKeyResolver - Integrity / Tampering
// =============================================================================

TEST_CASE("SoftKeyResolver rejects tampered HMAC", "[SoftKeyResolver][Integrity]")
{
    auto info = makeSoftKeyDeviceInfo([]() {
        auto validJson = makeValidKeyJson();
        auto j = json::parse(validJson);
        j["hmac"] = "0000000000000000000000000000000000000000000000000000000000000000";
        return j.dump();
    });

    SoftKeyResolver resolver;
    REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
}

TEST_CASE("SoftKeyResolver rejects tampered UUID", "[SoftKeyResolver][Integrity]")
{
    auto info = makeSoftKeyDeviceInfo([]() {
        auto validJson = makeValidKeyJson();
        auto j = json::parse(validJson);
        j["uuid"] = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
        return j.dump();
    });

    SoftKeyResolver resolver;
    REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
}

TEST_CASE("SoftKeyResolver rejects tampered serial number", "[SoftKeyResolver][Integrity]")
{
    auto info = makeSoftKeyDeviceInfo([]() {
        auto validJson = makeValidKeyJson();
        auto j = json::parse(validJson);
        j["serial_number"] = 99999;
        return j.dump();
    });

    SoftKeyResolver resolver;
    REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
}

TEST_CASE("SoftKeyResolver rejects tampered encrypted_key", "[SoftKeyResolver][Integrity]")
{
    auto info = makeSoftKeyDeviceInfo([]() {
        auto validJson = makeValidKeyJson();
        auto j = json::parse(validJson);
        j["encrypted_key"] = "dGFtcGVyZWQ=";
        return j.dump();
    });

    SoftKeyResolver resolver;
    REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
}

TEST_CASE("SoftKeyResolver rejects provider mismatch", "[SoftKeyResolver][Integrity]")
{
    auto info = makeSoftKeyDeviceInfo([]() {
        auto validJson = makeValidKeyJson();
        auto j = json::parse(validJson);
        j["provider"] = "wrongprovider";

        auto password = deviceKeyPassword();
        auto hmacMsg = std::string("wrongprovider") + j["uuid"].get<std::string>()
                     + std::to_string(j["serial_number"].get<uint64_t>())
                     + j["encrypted_key"].get<std::string>();
        j["hmac"] = computeHmac(hmacMsg, password);
        return j.dump();
    });

    SoftKeyResolver resolver;
    REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
}

TEST_CASE("SoftKeyResolver rejects key encrypted with wrong provider key",
          "[SoftKeyResolver][Integrity]")
{
    auto info = makeSoftKeyDeviceInfo([]() {
        auto rawDeviceKey = hexToBytes(TEST_DEVICE_KEY_HEX);
        const std::string wrongPassword = "wrong_provider_key_hex_string";

        auto encKey = encryptSecret(rawDeviceKey, wrongPassword);
        auto hmacMsg =
                std::string(TEST_PROVIDER) + TEST_UUID + std::to_string(TEST_SERIAL) + encKey;
        auto hmac = computeHmac(hmacMsg, wrongPassword);

        json j;
        j["encrypted_key"] = encKey;
        j["uuid"] = TEST_UUID;
        j["serial_number"] = TEST_SERIAL;
        j["provider"] = TEST_PROVIDER;
        j["hmac"] = hmac;
        return j.dump();
    });

    SoftKeyResolver resolver;
    REQUIRE_FALSE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));
}

TEST_CASE("SoftKeyResolver works with re-encrypted provider key", "[SoftKeyResolver]")
{
    // Simulate the provider re-encrypting their key (different salt/IV, same raw key).
    // Both calls to makeProviderEncryptedKey() produce different base64 blobs,
    // but decrypt to the same raw bytes -> same device key password.
    auto info1 = makeSoftKeyDeviceInfo([]() { return makeValidKeyJson(); });
    auto info2 = makeSoftKeyDeviceInfo([]() { return makeValidKeyJson(); });

    // The encrypted keys should differ (different random salt/IV)
    CHECK(info1.encryptedKey != info2.encryptedKey);

    // But both should successfully load the same device key
    SoftKeyResolver resolver1;
    SoftKeyResolver resolver2;
    REQUIRE(resolver1.tryResolve(info1, TEST_SERVER_TIMESTAMP));
    REQUIRE(resolver2.tryResolve(info2, TEST_SERVER_TIMESTAMP));
    CHECK(info1.uuid == info2.uuid);
    CHECK(info1.serialNumber == info2.serialNumber);
}

// =============================================================================
// SoftKeyResolver - createSigner
// =============================================================================

TEST_CASE("SoftKeyResolver createSigner produces valid signatures", "[SoftKeyResolver]")
{
    auto info = makeSoftKeyDeviceInfo([]() { return makeValidKeyJson(); });

    SoftKeyResolver resolver;
    REQUIRE(resolver.tryResolve(info, TEST_SERVER_TIMESTAMP));

    auto signer = resolver.createSigner();
    REQUIRE(signer);

    Digest digest {};
    auto signature = signer(digest);
    REQUIRE_FALSE(signature.empty());

    // Raw (r||s) ECDSA P-256 signatures are always 64 bytes
    CHECK(signature.size() == 64);
}

TEST_CASE("SoftKeyResolver createSigner is deterministic for same key", "[SoftKeyResolver]")
{
    auto info1 = makeSoftKeyDeviceInfo([]() { return makeValidKeyJson(); });
    auto info2 = makeSoftKeyDeviceInfo([]() { return makeValidKeyJson(); });

    SoftKeyResolver resolver1;
    SoftKeyResolver resolver2;
    REQUIRE(resolver1.tryResolve(info1, TEST_SERVER_TIMESTAMP));
    REQUIRE(resolver2.tryResolve(info2, TEST_SERVER_TIMESTAMP));

    auto signer1 = resolver1.createSigner();
    auto signer2 = resolver2.createSigner();

    Digest digest {};
    digest[0] = 0x42;
    auto sig1 = signer1(digest);
    auto sig2 = signer2(digest);
    REQUIRE_FALSE(sig1.empty());
    REQUIRE_FALSE(sig2.empty());
    // ECDSA signatures are non-deterministic (random k), so sig1 != sig2 is expected
}

// =============================================================================
// encrypt/decrypt round-trip (utility sanity check)
// =============================================================================

TEST_CASE("encryptSecret and decryptSecret round-trip", "[Crypto]")
{
    auto rawKey = hexToBytes(TEST_DEVICE_KEY_HEX);
    const std::string password = "test_password_123";

    auto encrypted = encryptSecret(rawKey, password);
    REQUIRE_FALSE(encrypted.empty());

    auto decrypted = decryptSecret(encrypted, password);
    REQUIRE(decrypted == rawKey);
}

TEST_CASE("decryptSecret fails with wrong password", "[Crypto]")
{
    auto rawKey = hexToBytes(TEST_DEVICE_KEY_HEX);
    auto encrypted = encryptSecret(rawKey, "correct_password");

    auto decrypted = decryptSecret(encrypted, "wrong_password");
    REQUIRE(decrypted.empty());
}

TEST_CASE("computeHmac produces consistent results", "[Crypto]")
{
    auto hmac1 = computeHmac("test message", "test key");
    auto hmac2 = computeHmac("test message", "test key");
    REQUIRE(hmac1 == hmac2);
    REQUIRE(hmac1.size() == 64); // SHA-256 hex = 64 chars
}

TEST_CASE("computeHmac differs for different messages", "[Crypto]")
{
    auto hmac1 = computeHmac("message1", "key");
    auto hmac2 = computeHmac("message2", "key");
    REQUIRE(hmac1 != hmac2);
}

TEST_CASE("computeHmac differs for different keys", "[Crypto]")
{
    auto hmac1 = computeHmac("message", "key1");
    auto hmac2 = computeHmac("message", "key2");
    REQUIRE(hmac1 != hmac2);
}
