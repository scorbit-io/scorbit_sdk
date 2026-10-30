/*
 * Scorbit SDK
 *
 * (c) 2026 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

// SB-6133: once a device has authenticated with the tap pad's TPM, an absent tap pad must defer
// authentication instead of loading or provisioning a soft key, which is a different identity.

#include <catch2/catch_test_macros.hpp>

#include "device_info.h"
#include "hardware_key_marker.h"
#include "nfc_flash.h"
#include "net.h"
#include "nfc_tpm_key_resolver.h"
#include "soft_key_resolver.h"
#include "utils/decrypt.h"

#include <utils/bytearray.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#if !defined(_WIN32)
#    include <unistd.h>
#endif

using namespace scorbit;
using namespace scorbit::detail;
namespace fs = std::filesystem;
using Dirs = std::vector<fs::path>;

namespace {

struct TempDir {
    TempDir()
    {
        std::random_device rd;
        path = fs::temp_directory_path() / ("scorbit-hwkey-" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::permissions(path, fs::perms::owner_all, fs::perm_options::add, ec);
        fs::remove_all(path, ec);
    }
    fs::path path;
};

void writeFile(const fs::path &file, const std::string &body)
{
    std::ofstream(file, std::ios::binary) << body;
}

const HardwareKeyIdentity PAD_A {1234, "0123456789abcdef0123456789abcdef"};
const HardwareKeyIdentity PAD_B {5678, "fedcba9876543210fedcba9876543210"};

// Test keys only, as in test_soft_key_resolver.cpp.
constexpr auto TEST_DEVICE_KEY_HEX =
        "a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2";
constexpr auto TEST_PROVIDER_KEY_HEX =
        "f1e2d3c4b5a69788796a5b4c3d2e1f00f1e2d3c4b5a69788796a5b4c3d2e1f00";
constexpr auto TEST_PROVIDER = "testprovider";
constexpr auto TEST_UUID = "c7f1fd0b-82f7-5504-8fbe-740c09bc7dab";
constexpr auto TIMESTAMP = "1773013652";

std::vector<uint8_t> hexToBytes(const std::string &hex)
{
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

std::string savedKeyJson()
{
    const auto providerKey = hexToBytes(TEST_PROVIDER_KEY_HEX);
    const auto password = utils::ByteArray(providerKey.data(), providerKey.size()).hex();
    const auto encKey = encryptSecret(hexToBytes(TEST_DEVICE_KEY_HEX), password);
    const auto hmac = computeHmac(std::string(TEST_PROVIDER) + TEST_UUID + "1" + encKey, password);
    return nlohmann::json {{"encrypted_key", encKey},
                           {"uuid", TEST_UUID},
                           {"serial_number", 1},
                           {"provider", TEST_PROVIDER},
                           {"hmac", hmac}}
            .dump();
}

// Soft-key provisioning that counts the saved-key loads and saves. The hostname is unreachable,
// so a provisioning attempt fails without leaving the host.
struct SoftKeySetup {
    std::shared_ptr<std::string> savedKey = std::make_shared<std::string>();
    std::shared_ptr<int> loads = std::make_shared<int>(0);
    std::shared_ptr<int> saves = std::make_shared<int>(0);

    DeviceInfo info() const
    {
        DeviceInfo info;
        info.provider = TEST_PROVIDER;
        info.hostname = "http://127.0.0.1:1";
        info.encryptedKey = encryptSecret(hexToBytes(TEST_PROVIDER_KEY_HEX),
                                          std::string(TEST_PROVIDER) + SCORBIT_SDK_ENCRYPT_SECRET);
        info.loadKeyCallback = [key = savedKey, loads = loads] {
            ++*loads;
            return *key;
        };
        info.saveKeyCallback = [saves = saves](const std::string &) { ++*saves; };
        return info;
    }
};

// Discovery that finds nothing: the tap pad is absent, and no host tty is probed.
TpmLocation noTapPad()
{
    return {};
}

} // namespace

// ------------------------------------ the marker ------------------------------------

TEST_CASE("Hardware-key marker: store, load and clear", "[HardwareKeyMarker]")
{
    TempDir dir;
    const HardwareKeyMarker marker {Dirs {dir.path}};

    CHECK_FALSE(marker.load());
    REQUIRE(marker.store(PAD_A));
    CHECK(marker.load() == PAD_A);

    const auto body = nlohmann::json::parse(
            std::ifstream(dir.path / HardwareKeyMarker::FILE_NAME, std::ios::binary));
    CHECK(body.at("serial").get<uint64_t>() == PAD_A.serial);
    CHECK(body.at("uuid").get<std::string>() == PAD_A.uuid);

    marker.clear();
    CHECK_FALSE(marker.load());
    CHECK_FALSE(fs::exists(dir.path / HardwareKeyMarker::FILE_NAME));
}

TEST_CASE("Hardware-key marker: a store replaces the old marker and leaves no staged file",
          "[HardwareKeyMarker]")
{
    TempDir dir;
    const HardwareKeyMarker marker {Dirs {dir.path}};
    REQUIRE(marker.store(PAD_A));
    REQUIRE(marker.store(PAD_B));
    CHECK(marker.load() == PAD_B);

    int files = 0;
    for ([[maybe_unused]] const auto &entry : fs::directory_iterator(dir.path)) {
        ++files;
    }
    CHECK(files == 1);
}

TEST_CASE("Hardware-key marker: record stores only a changed identity", "[HardwareKeyMarker]")
{
    TempDir dir;
    const HardwareKeyMarker marker {Dirs {dir.path}};
    const auto file = dir.path / HardwareKeyMarker::FILE_NAME;

    REQUIRE(marker.record(PAD_A));
    CHECK(marker.load() == PAD_A);
    const auto written = fs::last_write_time(file);

    // The same identity again: nothing is rewritten.
    fs::last_write_time(file, written - std::chrono::hours(1));
    REQUIRE(marker.record(PAD_A));
    CHECK((fs::last_write_time(file) == written - std::chrono::hours(1)));

    // A replaced tap pad: overwritten.
    REQUIRE(marker.record(PAD_B));
    CHECK(marker.load() == PAD_B);
}

TEST_CASE("Hardware-key marker: unreadable or malformed files count as absent",
          "[HardwareKeyMarker]")
{
    TempDir dir;
    const HardwareKeyMarker marker {Dirs {dir.path}};
    const auto file = dir.path / HardwareKeyMarker::FILE_NAME;

    SECTION("not JSON")
    {
        writeFile(file, "not json");
    }
    SECTION("empty")
    {
        writeFile(file, "");
    }
    SECTION("missing uuid")
    {
        writeFile(file, R"({"serial": 1})");
    }
    SECTION("wrong types")
    {
        writeFile(file, R"({"serial": "1", "uuid": 2})");
    }
    SECTION("empty uuid")
    {
        writeFile(file, R"({"serial": 1, "uuid": ""})");
    }
    SECTION("a directory in its place")
    {
        fs::create_directories(file);
    }
    CHECK_FALSE(marker.load());
}

TEST_CASE("Hardware-key marker: directories in order of preference", "[HardwareKeyMarker]")
{
    TempDir first;
    TempDir second;
    const HardwareKeyMarker marker {Dirs {first.path, second.path}};

    SECTION("stored in the first writable directory")
    {
        REQUIRE(marker.store(PAD_A));
        CHECK(fs::exists(first.path / HardwareKeyMarker::FILE_NAME));
        CHECK_FALSE(fs::exists(second.path / HardwareKeyMarker::FILE_NAME));
    }
    SECTION("a marker in the second directory alone counts")
    {
        REQUIRE(HardwareKeyMarker {Dirs {second.path}}.store(PAD_B));
        CHECK(marker.load() == PAD_B);
    }
    SECTION("the first directory's marker wins; a malformed one is skipped")
    {
        REQUIRE(HardwareKeyMarker {Dirs {first.path}}.store(PAD_A));
        REQUIRE(HardwareKeyMarker {Dirs {second.path}}.store(PAD_B));
        CHECK(marker.load() == PAD_A);
        writeFile(first.path / HardwareKeyMarker::FILE_NAME, "{");
        CHECK(marker.load() == PAD_B);
    }
    SECTION("a missing directory is created")
    {
        const HardwareKeyMarker nested {Dirs {second.path / "a" / "b"}};
        REQUIRE(nested.store(PAD_A));
        CHECK(nested.load() == PAD_A);
    }
    SECTION("clear removes it everywhere")
    {
        REQUIRE(HardwareKeyMarker {Dirs {first.path}}.store(PAD_A));
        REQUIRE(HardwareKeyMarker {Dirs {second.path}}.store(PAD_B));
        marker.clear();
        CHECK_FALSE(marker.load());
    }
}

#if !defined(_WIN32)
TEST_CASE("Hardware-key marker: a non-writable first directory falls back to the next",
          "[HardwareKeyMarker]")
{
    if (::geteuid() == 0) {
        SKIP("root writes into a read-only directory");
    }
    TempDir first;
    TempDir second;
    fs::permissions(first.path, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace);
    const HardwareKeyMarker marker {Dirs {first.path, second.path}};

    REQUIRE(marker.store(PAD_A));
    CHECK_FALSE(fs::exists(first.path / HardwareKeyMarker::FILE_NAME));
    CHECK(fs::exists(second.path / HardwareKeyMarker::FILE_NAME));
    CHECK(marker.load() == PAD_A);

    SECTION("no writable directory at all")
    {
        CHECK_FALSE(HardwareKeyMarker {Dirs {first.path}}.store(PAD_B));
        CHECK_FALSE(HardwareKeyMarker {Dirs {}}.store(PAD_B));
    }
}

TEST_CASE("Hardware-key marker: a link planted at the staged name is not written through",
          "[HardwareKeyMarker]")
{
    TempDir dir;
    TempDir elsewhere;
    const auto victim = elsewhere.path / "victim";
    writeFile(victim, "untouched");
    // The next staged name this process will use cannot be known, so plant links at a range.
    for (int n = 1; n < 200; ++n) {
        fs::create_symlink(victim, dir.path
                                           / (std::string(HardwareKeyMarker::FILE_NAME) + "."
                                              + std::to_string(::getpid()) + "." + std::to_string(n)
                                              + ".tmp"));
    }
    const HardwareKeyMarker marker {Dirs {dir.path}};
    marker.store(PAD_A);

    std::ifstream in(victim);
    const std::string body {std::istreambuf_iterator<char>(in), {}};
    CHECK(body == "untouched");
}
#endif

TEST_CASE("Hardware-key marker: the default directories end with the user's state directories",
          "[HardwareKeyMarker]")
{
    const auto dirs = HardwareKeyMarker::defaultDirs();
    const auto state = nfc_flash::RecordStore::defaultDirs();
    REQUIRE(dirs.size() >= state.size());
    CHECK(std::equal(state.begin(), state.end(), dirs.end() - std::ptrdiff_t(state.size())));
#if !defined(_WIN32)
    // The first is where this code was loaded from (the test executable here).
    CHECK(dirs.size() == state.size() + 1);
#endif
}

// ------------------------------------ the resolvers ------------------------------------

TEST_CASE("SoftKeyResolver with no marker loads its saved key as before", "[HardwareKeyMarker]")
{
    TempDir dir;
    SoftKeySetup setup;
    *setup.savedKey = savedKeyJson();
    auto info = setup.info();

    SoftKeyResolver resolver {std::make_shared<HardwareKeyMarker>(Dirs {dir.path})};
    CHECK(resolver.tryResolve(info, TIMESTAMP));
    CHECK_FALSE(resolver.shouldRetryLater());
    CHECK(*setup.loads == 1);
}

TEST_CASE("SoftKeyResolver with a marker neither loads nor provisions, and defers",
          "[HardwareKeyMarker]")
{
    TempDir dir;
    const auto marker = std::make_shared<HardwareKeyMarker>(Dirs {dir.path});
    REQUIRE(marker->store(PAD_A));
    SoftKeySetup setup;
    *setup.savedKey = savedKeyJson();
    auto info = setup.info();

    SoftKeyResolver resolver {marker};
    CHECK_FALSE(resolver.tryResolve(info, TIMESTAMP));
    CHECK(resolver.shouldRetryLater());
    CHECK_FALSE(resolver.tryResolve(info, TIMESTAMP)); // warned once; still refusing
    CHECK(resolver.shouldRetryLater());
    CHECK(*setup.loads == 0);
    CHECK(*setup.saves == 0);
    CHECK(info.uuid.empty());

    // The marker goes away: back to the soft key.
    marker->clear();
    CHECK(resolver.tryResolve(info, TIMESTAMP));
    CHECK_FALSE(resolver.shouldRetryLater());
}

TEST_CASE("SoftKeyResolver without soft-key provisioning fails as before, marker or not",
          "[HardwareKeyMarker]")
{
    TempDir dir;
    const auto marker = std::make_shared<HardwareKeyMarker>(Dirs {dir.path});
    REQUIRE(marker->store(PAD_A));
    DeviceInfo info;
    SoftKeyResolver resolver {marker};
    CHECK_FALSE(resolver.tryResolve(info, TIMESTAMP));
    CHECK_FALSE(resolver.shouldRetryLater());
}

TEST_CASE("Tap pad absent with a marker: key resolution defers and no soft key is touched",
          "[HardwareKeyMarker]")
{
    TempDir dir;
    const auto marker = std::make_shared<HardwareKeyMarker>(Dirs {dir.path});
    REQUIRE(marker->store(PAD_A));
    SoftKeySetup setup;
    *setup.savedKey = savedKeyJson();

    std::vector<std::unique_ptr<IKeyResolver>> resolvers;
    resolvers.push_back(std::make_unique<NfcTpmKeyResolver>(noTapPad, marker));
    resolvers.push_back(std::make_unique<SoftKeyResolver>(marker));
    Net net {setup.info(), std::move(resolvers), marker};

    CHECK(NetTestAccess::resolveKeys(net, TIMESTAMP) == KeyResolution::Deferred);
    CHECK(*setup.loads == 0);
    CHECK(*setup.saves == 0);
    CHECK(NetTestAccess::keyResolverCount(net) == 2);
    CHECK(marker->load() == PAD_A);
}

TEST_CASE("Tap pad absent with no marker: the soft key is used as before", "[HardwareKeyMarker]")
{
    TempDir dir;
    const auto marker = std::make_shared<HardwareKeyMarker>(Dirs {dir.path});
    SoftKeySetup setup;
    *setup.savedKey = savedKeyJson();

    std::vector<std::unique_ptr<IKeyResolver>> resolvers;
    resolvers.push_back(std::make_unique<NfcTpmKeyResolver>(noTapPad, marker));
    resolvers.push_back(std::make_unique<SoftKeyResolver>(marker));
    Net net {setup.info(), std::move(resolvers), marker};

    CHECK(NetTestAccess::resolveKeys(net, TIMESTAMP) == KeyResolution::Resolved);
    CHECK(*setup.loads == 1);
    CHECK_FALSE(marker->load()); // a soft key never writes the marker
}

TEST_CASE("A resolved tap pad key records its identity in the marker", "[HardwareKeyMarker]")
{
    TempDir dir;
    const auto marker = std::make_shared<HardwareKeyMarker>(Dirs {dir.path});
    DeviceInfo info;
    info.serialNumber = PAD_A.serial;
    info.uuid = PAD_A.uuid;

    noteHardwareKey(marker.get(), info);
    CHECK(marker->load() == PAD_A);

    // A replaced tap pad overwrites it.
    info.serialNumber = PAD_B.serial;
    info.uuid = PAD_B.uuid;
    noteHardwareKey(marker.get(), info);
    CHECK(marker->load() == PAD_B);

    // No uuid is no identity; no marker at all is fine.
    info.uuid.clear();
    noteHardwareKey(marker.get(), info);
    CHECK(marker->load() == PAD_B);
    noteHardwareKey(nullptr, info);

    CHECK(NfcTpmKeyResolver {noTapPad}.isHardwareKey());
    CHECK_FALSE(SoftKeyResolver {}.isHardwareKey());
}
