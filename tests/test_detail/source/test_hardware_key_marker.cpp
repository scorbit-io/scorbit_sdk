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
#include <catch2/generators/catch_generators.hpp>

#include "device_info.h"
#include "net.h"
#include "net_util.h"
#include "nfc_tpm_key_resolver.h"
#include "soft_key_resolver.h"
#include "utils/decrypt.h"

#include <tpm/hardware_key_marker.h>
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
#    include <pwd.h>
#    include <sys/stat.h>
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
        fs::permissions(path, fs::perms::owner_all, fs::perm_options::replace); // umask-proof
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

// A tap pad TPM key as resolveKeys() sees one: resolves to the given identity, or reports busy.
struct FakeHardwareResolver : IKeyResolver {
    FakeHardwareResolver(uint64_t serial, std::string uuid, bool busy = false)
        : serial(serial)
        , uuid(std::move(uuid))
        , busy(busy)
    {
    }

    bool tryResolve(DeviceInfo &info, const std::string &) override
    {
        if (busy) {
            return false;
        }
        info.serialNumber = serial;
        info.uuid = uuid;
        return true;
    }
    bool shouldRetryLater() const override { return busy; }
    bool isHardwareKey() const override { return true; }
    SignerCallback createSigner() const override
    {
        return [](const Digest &) { return Signature {0x01}; };
    }

    uint64_t serial;
    std::string uuid;
    bool busy;
};

KeyResolution resolveWith(std::unique_ptr<IKeyResolver> resolver,
                          const std::shared_ptr<const HardwareKeyMarker> &marker,
                          DeviceInfo info = {})
{
    std::vector<std::unique_ptr<IKeyResolver>> resolvers;
    resolvers.push_back(std::move(resolver));
    resolvers.push_back(std::make_unique<SoftKeyResolver>(marker));
    Net net {std::move(info), std::move(resolvers), marker};
    return NetTestAccess::resolveKeys(net, TIMESTAMP);
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

    // Another instance, e.g. after a restart, cannot know the marker it finds was synced, so it
    // stores it once more, then no more.
    const HardwareKeyMarker restarted {Dirs {dir.path}};
    fs::last_write_time(file, written - std::chrono::hours(2));
    REQUIRE(restarted.record(PAD_B));
    const auto rewritten = fs::last_write_time(file);
    CHECK((rewritten != written - std::chrono::hours(2)));
    fs::last_write_time(file, rewritten - std::chrono::hours(1));
    REQUIRE(restarted.record(PAD_B));
    CHECK((fs::last_write_time(file) == rewritten - std::chrono::hours(1)));
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

TEST_CASE("Hardware-key marker: a directory others can write to is not trusted",
          "[HardwareKeyMarker]")
{
    TempDir loose;
    const auto perms = GENERATE(fs::perms::group_write, fs::perms::others_write);
    fs::permissions(loose.path, perms, fs::perm_options::add);

    // A marker in it is ignored, and clear leaves it alone.
    writeFile(loose.path / HardwareKeyMarker::FILE_NAME,
              nlohmann::json {{"serial", PAD_A.serial}, {"uuid", PAD_A.uuid}}.dump());
    const HardwareKeyMarker marker {Dirs {loose.path}};
    CHECK_FALSE(marker.load());
    marker.clear();
    CHECK(fs::exists(loose.path / HardwareKeyMarker::FILE_NAME));

    // A store, the directory being this user's, makes it private first and then uses it.
    REQUIRE(marker.store(PAD_B));
    CHECK((fs::status(loose.path).permissions() & fs::perms::all) == fs::perms::owner_all);
    CHECK(marker.load() == PAD_B);
}

TEST_CASE("Hardware-key marker: a directory under one others can write to is not trusted",
          "[HardwareKeyMarker]")
{
    TempDir root;
    const auto parent = root.path / "parent";
    const auto dir = parent / "scorbit";
    fs::create_directories(dir);
    fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace);
    writeFile(dir / HardwareKeyMarker::FILE_NAME,
              nlohmann::json {{"serial", PAD_A.serial}, {"uuid", PAD_A.uuid}}.dump());
    const HardwareKeyMarker marker {Dirs {dir}};
    REQUIRE(marker.load() == PAD_A);

    // Others could rename the marker's directory away.
    fs::permissions(parent, fs::perms::group_write | fs::perms::others_write,
                    fs::perm_options::add);
    CHECK_FALSE(marker.load());
    CHECK_FALSE(marker.store(PAD_B));

    // Unless the parent is sticky, as /tmp is.
    fs::permissions(parent, fs::perms::sticky_bit, fs::perm_options::add);
    CHECK(marker.load() == PAD_A);
}

TEST_CASE("Hardware-key marker: directories a store creates are private whatever the umask",
          "[HardwareKeyMarker]")
{
    TempDir root;
    const auto dir = root.path / "local" / "state" / "scorbit";
    const auto old = ::umask(0);
    const bool stored = HardwareKeyMarker {Dirs {dir}}.store(PAD_A);
    ::umask(old);

    REQUIRE(stored);
    for (const auto &p : {root.path / "local", root.path / "local" / "state", dir}) {
        CHECK((fs::status(p).permissions() & fs::perms::all) == fs::perms::owner_all);
    }
    CHECK(HardwareKeyMarker {Dirs {dir}}.load() == PAD_A);
}

TEST_CASE("Hardware-key marker: a link in a directory others can write to is not trusted",
          "[HardwareKeyMarker]")
{
    TempDir root;
    const auto loose = root.path / "loose";
    const auto real = root.path / "private" / "state";
    fs::create_directories(loose);
    fs::create_directories(real / "scorbit");
    fs::create_directory_symlink(real, loose / "state");
    const auto dir = loose / "state" / "scorbit";
    REQUIRE(HardwareKeyMarker {Dirs {dir}}.store(PAD_A));

    // The target stays private, but anyone could now delete the link and the marker with it.
    fs::permissions(loose, fs::perms::others_write, fs::perm_options::add);
    CHECK_FALSE(HardwareKeyMarker {Dirs {dir}}.load());
    CHECK(HardwareKeyMarker {Dirs {real / "scorbit"}}.load() == PAD_A);
}

TEST_CASE("Hardware-key marker: a FIFO in place of the marker is ignored without blocking",
          "[HardwareKeyMarker]")
{
    TempDir dir;
    REQUIRE(::mkfifo((dir.path / HardwareKeyMarker::FILE_NAME).c_str(), 0600) == 0);
    CHECK_FALSE(HardwareKeyMarker {Dirs {dir.path}}.load());
}

TEST_CASE("Hardware-key marker: record fails while an older marker it cannot replace wins",
          "[HardwareKeyMarker]")
{
    if (::geteuid() == 0) {
        SKIP("root writes into a read-only directory");
    }
    TempDir first;
    TempDir second;
    REQUIRE(HardwareKeyMarker {Dirs {first.path}}.store(PAD_A));
    fs::permissions(first.path, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace);
    const HardwareKeyMarker marker {Dirs {first.path, second.path}};

    CHECK_FALSE(marker.record(PAD_B));
    CHECK(marker.load() == PAD_A); // still a marker, so still no soft key
}

TEST_CASE("Hardware-key marker: links and oversized files are not read", "[HardwareKeyMarker]")
{
    TempDir dir;
    TempDir elsewhere;
    REQUIRE(HardwareKeyMarker {Dirs {elsewhere.path}}.store(PAD_A));

    SECTION("a link in place of the marker")
    {
        fs::create_symlink(elsewhere.path / HardwareKeyMarker::FILE_NAME,
                           dir.path / HardwareKeyMarker::FILE_NAME);
        CHECK_FALSE(HardwareKeyMarker {Dirs {dir.path}}.load());
    }
    SECTION("a link in place of the directory")
    {
        const auto link = dir.path / "linked";
        fs::create_directory_symlink(elsewhere.path, link);
        const HardwareKeyMarker marker {Dirs {link}};
        CHECK_FALSE(marker.load());
        CHECK_FALSE(marker.store(PAD_B));
        marker.clear();
        CHECK(fs::exists(elsewhere.path / HardwareKeyMarker::FILE_NAME));
    }
    SECTION("larger than any marker")
    {
        const auto padded = nlohmann::json {{"serial", PAD_A.serial},
                                            {"uuid", PAD_A.uuid},
                                            {"pad", std::string(5000, 'x')}}
                                    .dump();
        writeFile(dir.path / HardwareKeyMarker::FILE_NAME, padded);
        CHECK_FALSE(HardwareKeyMarker {Dirs {dir.path}}.load());
    }
}
#endif

TEST_CASE("Hardware-key marker: the default directory is the user's state directory",
          "[HardwareKeyMarker]")
{
    const auto dirs = HardwareKeyMarker::defaultDirs();
    Dirs expected;
    if (const char *state = std::getenv("XDG_STATE_HOME");
        state && *state && fs::path(state).is_absolute()) {
        expected.push_back(fs::path(state) / "scorbit");
    } else if (const char *home = std::getenv("HOME"); home && *home) {
        expected.push_back(fs::path(home) / ".local" / "state" / "scorbit");
    }
#if !defined(_WIN32)
    else if (const passwd *pw = ::getpwuid(::geteuid()); pw && pw->pw_dir && *pw->pw_dir) {
        expected.push_back(fs::path(pw->pw_dir) / ".local" / "state" / "scorbit");
    }
#endif
#if defined(_WIN32)
    if (const char *local = std::getenv("LOCALAPPDATA"); local && *local) {
        expected.push_back(fs::path(local) / "scorbit");
    }
#endif
    // Nothing else: the same for every process of this user, and never the temp directory.
    CHECK(dirs == expected);
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
    resolvers.push_back(std::make_unique<NfcTpmKeyResolver>(noTapPad));
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
    resolvers.push_back(std::make_unique<NfcTpmKeyResolver>(noTapPad));
    resolvers.push_back(std::make_unique<SoftKeyResolver>(marker));
    Net net {setup.info(), std::move(resolvers), marker};

    CHECK(NetTestAccess::resolveKeys(net, TIMESTAMP) == KeyResolution::Resolved);
    CHECK(*setup.loads == 1);
    CHECK_FALSE(marker->load()); // a soft key never writes the marker
}

TEST_CASE("Resolving a hardware key records no marker before the api accepts it",
          "[HardwareKeyMarker]")
{
    TempDir dir;
    const auto marker = std::make_shared<HardwareKeyMarker>(Dirs {dir.path});
    REQUIRE(resolveWith(std::make_unique<FakeHardwareResolver>(PAD_A.serial, TEST_UUID), marker)
            == KeyResolution::Resolved);
    CHECK_FALSE(marker->load()); // test_net_auth_retry.cpp covers the accepted case

    CHECK(NfcTpmKeyResolver {noTapPad}.isHardwareKey());
    CHECK_FALSE(SoftKeyResolver {}.isHardwareKey());
}

TEST_CASE("A hardware key with an invalid uuid records no marker", "[HardwareKeyMarker]")
{
    TempDir dir;
    const auto marker = std::make_shared<HardwareKeyMarker>(Dirs {dir.path});
    CHECK(resolveWith(std::make_unique<FakeHardwareResolver>(PAD_A.serial, "not-a-uuid"), marker)
          != KeyResolution::Resolved);
    CHECK_FALSE(marker->load());
}

TEST_CASE("A busy hardware key with a marker defers and leaves the soft key and marker alone",
          "[HardwareKeyMarker]")
{
    TempDir dir;
    const auto marker = std::make_shared<HardwareKeyMarker>(Dirs {dir.path});
    REQUIRE(marker->store(PAD_A));
    SoftKeySetup setup;
    *setup.savedKey = savedKeyJson();

    CHECK(resolveWith(std::make_unique<FakeHardwareResolver>(PAD_B.serial, PAD_B.uuid, true),
                      marker, setup.info())
          == KeyResolution::Deferred);
    CHECK(*setup.loads == 0);
    CHECK(*setup.saves == 0);
    CHECK(marker->load() == PAD_A);
}
