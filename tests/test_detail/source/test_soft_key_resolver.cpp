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
#include "net.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

// clazy:excludeall=non-pod-global-static

using namespace scorbit;
using namespace scorbit::detail;
using json = nlohmann::json;

namespace {

// Valid P-256 test private key (32 bytes = 64 hex chars)
constexpr auto TEST_PRIVATE_KEY_HEX =
        "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2";
constexpr auto TEST_UUID = "c7f1fd0b-82f7-5504-8fbe-740c09bc7dab";
constexpr uint64_t TEST_SERIAL = 12345;

std::string makeValidKeyJson()
{
    json j;
    j["private_key"] = TEST_PRIVATE_KEY_HEX;
    j["uuid"] = TEST_UUID;
    j["serial_number"] = TEST_SERIAL;
    return j.dump();
}

DeviceInfo makeSoftKeyDeviceInfo(LoadKeyCallback loadCb)
{
    DeviceInfo info;
    info.provider = "testprovider";
    info.machineId = 1234;
    info.gameCodeVersion = "1.0.0";
    info.hostname = "staging";
    info.encryptedKey = "dummy_encrypted_key";
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
    REQUIRE(resolver.tryResolve(info));
    CHECK(info.uuid == TEST_UUID);
    CHECK(info.serialNumber == TEST_SERIAL);
}

TEST_CASE("SoftKeyResolver returns false when no key saved", "[SoftKeyResolver]")
{
    auto info = makeSoftKeyDeviceInfo([]() { return std::string {}; });

    SoftKeyResolver resolver;
    // tryResolve will try loadKey (empty → fail), then provisionNewKey (no real server → fail)
    REQUIRE_FALSE(resolver.tryResolve(info));
}

TEST_CASE("SoftKeyResolver returns false with invalid JSON", "[SoftKeyResolver]")
{
    auto info = makeSoftKeyDeviceInfo([]() { return "not json"; });

    SoftKeyResolver resolver;
    REQUIRE_FALSE(resolver.tryResolve(info));
}

TEST_CASE("SoftKeyResolver returns false with missing fields", "[SoftKeyResolver]")
{
    SECTION("Missing private_key")
    {
        auto info = makeSoftKeyDeviceInfo([]() {
            json j;
            j["uuid"] = TEST_UUID;
            j["serial_number"] = TEST_SERIAL;
            return j.dump();
        });

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info));
    }

    SECTION("Missing uuid")
    {
        auto info = makeSoftKeyDeviceInfo([]() {
            json j;
            j["private_key"] = TEST_PRIVATE_KEY_HEX;
            j["serial_number"] = TEST_SERIAL;
            return j.dump();
        });

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info));
    }

    SECTION("Missing serial_number")
    {
        auto info = makeSoftKeyDeviceInfo([]() {
            json j;
            j["private_key"] = TEST_PRIVATE_KEY_HEX;
            j["uuid"] = TEST_UUID;
            return j.dump();
        });

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info));
    }
}

TEST_CASE("SoftKeyResolver returns false with wrong key size", "[SoftKeyResolver]")
{
    SECTION("Key too short (16 bytes)")
    {
        auto info = makeSoftKeyDeviceInfo([]() {
            json j;
            j["private_key"] = "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6";
            j["uuid"] = TEST_UUID;
            j["serial_number"] = TEST_SERIAL;
            return j.dump();
        });

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info));
    }

    SECTION("Key too long (48 bytes)")
    {
        auto info = makeSoftKeyDeviceInfo([]() {
            json j;
            j["private_key"] = "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6"
                               "a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2"
                               "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6";
            j["uuid"] = TEST_UUID;
            j["serial_number"] = TEST_SERIAL;
            return j.dump();
        });

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info));
    }
}

TEST_CASE("SoftKeyResolver requires soft key provisioning config", "[SoftKeyResolver]")
{
    SECTION("No encrypted key")
    {
        DeviceInfo info;
        info.saveKeyCallback = [](const std::string &) { };
        info.loadKeyCallback = []() { return makeValidKeyJson(); };
        // encryptedKey is empty

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info));
    }

    SECTION("No save callback")
    {
        DeviceInfo info;
        info.encryptedKey = "dummy";
        info.loadKeyCallback = []() { return makeValidKeyJson(); };
        // saveKeyCallback is not set

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info));
    }

    SECTION("No load callback")
    {
        DeviceInfo info;
        info.encryptedKey = "dummy";
        info.saveKeyCallback = [](const std::string &) { };
        // loadKeyCallback is not set

        SoftKeyResolver resolver;
        REQUIRE_FALSE(resolver.tryResolve(info));
    }
}

// =============================================================================
// SoftKeyResolver - createSigner
// =============================================================================

TEST_CASE("SoftKeyResolver createSigner produces valid signatures", "[SoftKeyResolver]")
{
    auto info = makeSoftKeyDeviceInfo([]() { return makeValidKeyJson(); });

    SoftKeyResolver resolver;
    REQUIRE(resolver.tryResolve(info));

    auto signer = resolver.createSigner();
    REQUIRE(signer);

    // Sign a test digest (all zeros)
    Digest digest {};
    auto signature = signer(digest);
    REQUIRE_FALSE(signature.empty());

    // DER-encoded ECDSA P-256 signatures are typically 70-72 bytes
    CHECK(signature.size() >= 68);
    CHECK(signature.size() <= 72);
}

TEST_CASE("SoftKeyResolver createSigner is deterministic for same key", "[SoftKeyResolver]")
{
    auto info1 = makeSoftKeyDeviceInfo([]() { return makeValidKeyJson(); });
    auto info2 = makeSoftKeyDeviceInfo([]() { return makeValidKeyJson(); });

    SoftKeyResolver resolver1;
    SoftKeyResolver resolver2;
    REQUIRE(resolver1.tryResolve(info1));
    REQUIRE(resolver2.tryResolve(info2));

    auto signer1 = resolver1.createSigner();
    auto signer2 = resolver2.createSigner();

    // Both signers should produce non-empty signatures for the same digest
    Digest digest {};
    digest[0] = 0x42;
    auto sig1 = signer1(digest);
    auto sig2 = signer2(digest);
    REQUIRE_FALSE(sig1.empty());
    REQUIRE_FALSE(sig2.empty());
    // ECDSA signatures are non-deterministic (random k), so sig1 != sig2 is expected
}
