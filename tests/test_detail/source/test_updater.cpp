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

#include <scorbit_sdk/version.h>
#include <updater.h>
#include "device_info.h"
#include "net_util.h"
#include "trompeloeil_printer.h"
#include "executable_bytes.h"
#include <utils/archiver.h>

#include <nlohmann/json.hpp>
#include <catch2/catch_test_macros.hpp>
#include <trompeloeil.hpp>
#include <boost/filesystem.hpp>
#include <boost/predef.h>
#include <fstream>
#include <iterator>

#if !BOOST_OS_WINDOWS
#    include <unistd.h>
#endif

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

namespace {

class MockNetBase : public NetBase
{
public:
    virtual ~MockNetBase() = default;
    AuthStatus status() const override { return AuthStatus::NotAuthenticated; };
    void requestPairCode(StringCallback) override {};
    const std::string &getMachineUuid() const override
    {
        static std::string rv;
        return rv;
    };
    std::uint64_t getMachineSerial() const override { return 0; };
    const std::string &getPairDeeplink() const override
    {
        static std::string rv;
        return rv;
    };
    const DeviceInfo &deviceInfo() const override
    {
        static DeviceInfo info;
        info.gameCodeVersion = "1.2.3";
        return info;
    };
    void requestTopScores(LeaderboardScope, LeaderboardPeriod, const std::string &,
                          LeaderboardVpinFilter, LeaderboardHandleCallback) override {};
    void requestUnpair(StringCallback) override {};
    void authenticate() override {};
    void sessionCreate(const scorbit::detail::GameData &, GameStartOrigin,
                       std::function<void()>) override {};
    void submitGameData(const GameData &, SessionFlags) override {};
    void getConfig() override {};

    MAKE_MOCK5(updateConfig,
               void(const std::string &, const std::string &, bool, std::optional<std::string>,
                    HttpStatusCallback),
               override);
    MAKE_MOCK5(download,
               void(bool isAsync, StringCallback, const std::string &, const std::string &,
                    const HttpHeaders &),
               override);

    void downloadBuffer(bool isAsync, VectorCallback, const std::string &, size_t,
                        const HttpHeaders &) override {};
    PlayerProfilesManager &playersManager() override { return m_playersManager; };
    void patchScorbitron(std::string, StringCallback, std::vector<AuthStatus>) override {};
    std::string consumeNonce() override { return {}; };
    void requestPairMachine(const std::string &, const std::string &, StringCallback) override {};
    void setCapabilities(Capabilities capabilities) override {};
    void setCreditsDropped(int, const std::string &, bool) override {};
    void setCreditsStatus(bool, int, int, const char *) override {};
    void submitHardwareProbeResult(const std::string &, const std::string &,
                                   HttpStatusCallback) override {};

private:
    PlayerProfilesManager m_playersManager;
};

class TestableUpdater : public Updater
{
public:
    using Updater::Updater;

protected:
    boost::filesystem::path getSdkLibraryPath() const override
    {
        return "/fake/libscorbit_sdk.dylib";
    }
    boost::filesystem::path getProcessExecutablePath() const override
    {
        return "/fake/test_binary";
    }
    // On Linux the real ones look up and remount /fake's filesystem, which does not exist; tests
    // must never remount the host's (SB-6104).
    utils::MakeWritableResult makeWritable(const boost::filesystem::path &) const override
    {
        return {true, {}};
    }
    bool remountReadOnly(const std::string &) const override { return true; }
};

// Updates a library that really exists, in a temp dir
class TempDirUpdater : public Updater
{
public:
    TempDirUpdater(NetBase &net, boost::filesystem::path library)
        : Updater(net, false, "1.99.30", "test_platform")
        , m_library {std::move(library)}
    {
    }

protected:
    boost::filesystem::path getSdkLibraryPath() const override { return m_library; }
    boost::filesystem::path getProcessExecutablePath() const override
    {
        return "/fake/test_binary";
    }
    // As TestableUpdater: never remount the host's /tmp (SB-6104).
    utils::MakeWritableResult makeWritable(const boost::filesystem::path &) const override
    {
        return {true, {}};
    }
    bool remountReadOnly(const std::string &) const override { return true; }

private:
    boost::filesystem::path m_library;
};

std::string readFile(const boost::filesystem::path &path)
{
    std::ifstream ifs(path.string(), std::ios::binary);
    return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("Updater")
{
    nlohmann::json json = nlohmann::json::parse(R"(
            {
                "sdk": {
                    "version": "1.0.2",
                    "assets_json": [
                        "https://example.com/scorbit_sdk-1.0.2-arm64_testabi.tgz",
                        "https://example.com/scorbit_sdk-1.0.2-arm_testabi.tgz",
                        "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz",
                        "https://example.com/scorbit_sdk-1.0.2-amd64_testabi.tgz"
                    ]
                }
            }
        )");

    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into Updater, so we keep the ref

    // Create Updater object with mocked NetBase
    TestableUpdater updater(*mockNet, false, "1.99.30", "test_platform");

    SECTION("happy path")
    {
        REQUIRE_CALL(mockNetRef,
                     download(false, _,
                              "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz", _, _))
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json, nullptr);
    }

    SECTION("download error")
    {
        REQUIRE_CALL(mockNetRef,
                     download(false, _,
                              "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz", _, _))
                .LR_SIDE_EFFECT(_2(Error::ApiError, "some_temp_file.tar.gz");)
                .TIMES(1);

        REQUIRE_CALL(mockNetRef, updateConfig(eq("sdk"), eq("1.0.1"), eq(false), _, _))
                .WITH(_4.has_value()
                      && _4->find("Updater: download failed: 4, some_temp_file.tar.gz")
                                 != std::string::npos)
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json, nullptr);
    }

    SECTION("incorrect version, can't find download")
    {
        json["sdk"]["version"] = "1.0.0";
        REQUIRE_CALL(mockNetRef, updateConfig(eq("sdk"), eq(SCORBIT_SDK_VERSION), eq(false),
                                              ANY(std::optional<std::string>), _))
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json, nullptr);
    }

    SECTION("empty assets")
    {
        json["sdk"]["assets_json"].clear();
        REQUIRE_CALL(mockNetRef, updateConfig(eq("sdk"), eq(SCORBIT_SDK_VERSION), eq(false), _, _))
                .WITH(_4.has_value()
                      && _4->find("Couldn't find update file in assets") != std::string::npos)
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json, nullptr);
    }
}

// Make sure that if current version is x.y.z then it updates only by x.y.*
TEST_CASE("Updater major.minor version mismatch")
{
    nlohmann::json json = nlohmann::json::parse(R"(
            {
                "sdk": {
                    "version": "1.1.0",
                    "assets_json": [
                        "https://example.com/scorbit_sdk-1.1.0-testarch_testabi.tgz"
                    ]
                }
            }
        )");

    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet;

    // Create Updater object with mocked NetBase
    TestableUpdater updater(*mockNet, false, "1.99.30", "test_platform");

    REQUIRE_CALL(mockNetRef, updateConfig(eq("sdk"), eq(SCORBIT_SDK_VERSION), eq(false), _, _))
            .WITH(_4.has_value()
                  && _4->find("Version mismatch: can only update by 1.0.x, found: 1.1.0")
                             != std::string::npos)
            .TIMES(1);

    updater.checkNewVersionAndUpdate(json, nullptr);
}

// Make sure that if using encrypted key, sdk key hash and prod key hash should match
TEST_CASE("Updater prod key hash check")
{
    nlohmann::json json = nlohmann::json::parse(R"(
            {
                "sdk": {
                    "version": "1.0.2",
                    "assets_json": [
                        "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz"
                    ]
                }
            }
        )");

    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet;

    SECTION("Mismatch while using encrypted key")
    {
        // Create Updater object with mocked NetBase
        TestableUpdater updater(*mockNet, true, "1.99.30", "test_platform");

        REQUIRE_CALL(mockNetRef, updateConfig(eq("sdk"), eq(SCORBIT_SDK_VERSION), eq(false), _, _))
                .WITH(_4.has_value()
                      && _4->find("Using encrypted key, production key hash mismatch: "
                                  "expected unknown1, found unknown2")
                                 != std::string::npos)
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json, nullptr);
    }

    SECTION("No problem with mismatch while not using encrypted key")
    {
        // Create Updater object with mocked NetBase
        TestableUpdater updater(*mockNet, false, "1.99.30", "test_platform");

        REQUIRE_CALL(mockNetRef,
                     download(false, _,
                              "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz", _, _))
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json, nullptr);
    }
}

TEST_CASE("Updater replaces the library only with a good one")
{
    namespace fs = boost::filesystem;

    nlohmann::json json = nlohmann::json::parse(R"(
            {
                "sdk": {
                    "version": "1.0.2",
                    "assets_json": [
                        "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz"
                    ]
                }
            }
        )");

    const auto dir = fs::temp_directory_path() / fs::unique_path("test_updater_%%%%-%%%%");
    fs::create_directories(dir);
    std::shared_ptr<void> removeDir(nullptr, [&dir](void *) { fs::remove_all(dir); });

    const auto library = dir / "libscorbit_sdk.so";
    const auto original = executableBytes("version 1.0.1");
    {
        std::ofstream ofs(library.string(), std::ios::binary);
        ofs << original;
    }

    MockNetBase mockNet;
    TempDirUpdater updater(mockNet, library);

    SECTION("Good archive replaces the library")
    {
        const auto updated = executableBytes("version 1.0.2");

        REQUIRE_CALL(mockNet, download(false, _, _, _, _))
                .LR_SIDE_EFFECT(REQUIRE(createTarGz(_4, {}, {{"lib/libscorbit_sdk.so", updated}}));
                                _2(Error::Success, "");)
                .TIMES(1);
        REQUIRE_CALL(mockNet, updateConfig(eq("sdk"), eq(SCORBIT_SDK_VERSION), eq(true), _, _))
                .WITH(_4.has_value() && _4->find("Updated successfully") != std::string::npos)
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json, nullptr);

        CHECK(readFile(library) == updated);
    }

    SECTION("The installed version offered again before a restart is left alone")
    {
        const auto updated = executableBytes("version 1.0.2");

        // Once only: the second offer neither downloads nor reports
        REQUIRE_CALL(mockNet, download(false, _, _, _, _))
                .LR_SIDE_EFFECT(REQUIRE(createTarGz(_4, {}, {{"lib/libscorbit_sdk.so", updated}}));
                                _2(Error::Success, "");)
                .TIMES(1);
        REQUIRE_CALL(mockNet, updateConfig(eq("sdk"), eq(SCORBIT_SDK_VERSION), eq(true), _, _))
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json, nullptr);
        updater.checkNewVersionAndUpdate(json, nullptr);

        CHECK(readFile(library) == updated);
    }

    SECTION("Archive with an empty library keeps the current one")
    {
        REQUIRE_CALL(mockNet, download(false, _, _, _, _))
                .LR_SIDE_EFFECT(REQUIRE(createTarGz(_4, {}, {{"lib/libscorbit_sdk.so", ""}}));
                                _2(Error::Success, "");)
                .TIMES(1);
        REQUIRE_CALL(mockNet, updateConfig(eq("sdk"), eq(SCORBIT_SDK_VERSION), eq(false), _, _))
                .WITH(_4.has_value() && _4->find("Empty file") != std::string::npos)
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json, nullptr);

        CHECK(readFile(library) == original);
    }

    // Nothing is left next to the library
    CHECK_FALSE(fs::exists(dir / "scorbit_update"));
    CHECK_FALSE(fs::exists(library.string() + ".old"));
    CHECK_FALSE(fs::exists(library.string() + ".new"));
}

#if !BOOST_OS_WINDOWS
// Files left by an interrupted update could be taken for the new ones
TEST_CASE("Updater stops when it can't clear the update directory")
{
    namespace fs = boost::filesystem;

    if (::geteuid() == 0) {
        SKIP("root can remove from a read-only directory");
    }

    nlohmann::json json = nlohmann::json::parse(R"(
            {
                "sdk": {
                    "version": "1.0.2",
                    "assets_json": [
                        "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz"
                    ]
                }
            }
        )");

    const auto dir = fs::temp_directory_path() / fs::unique_path("test_updater_%%%%-%%%%");
    const auto locked = dir / "scorbit_update" / "lib";
    fs::create_directories(locked);
    {
        std::ofstream ofs((locked / "libscorbit_sdk.so").string(), std::ios::binary);
        ofs << executableBytes("version 1.0.0, left by an interrupted update");
    }
    fs::permissions(locked, fs::owner_read | fs::owner_exe);
    std::shared_ptr<void> removeDir(nullptr, [&dir, &locked](void *) {
        fs::permissions(locked, fs::owner_all);
        fs::remove_all(dir);
    });

    const auto library = dir / "libscorbit_sdk.so";
    const auto original = executableBytes("version 1.0.1");
    {
        std::ofstream ofs(library.string(), std::ios::binary);
        ofs << original;
    }

    MockNetBase mockNet;
    TempDirUpdater updater(mockNet, library);

    REQUIRE_CALL(mockNet, download(false, _, _, _, _))
            .LR_SIDE_EFFECT(REQUIRE(createTarGz(_4, {}, {{"README", "no library in here"}}));
                            _2(Error::Success, "");)
            .TIMES(1);
    REQUIRE_CALL(mockNet, updateConfig(eq("sdk"), eq(SCORBIT_SDK_VERSION), eq(false), _, _))
            .WITH(_4.has_value()
                  && _4->find("Can't clear the update directory") != std::string::npos)
            .TIMES(1);

    updater.checkNewVersionAndUpdate(json, nullptr);

    CHECK(readFile(library) == original);
}
#endif

TEST_CASE("Updater relies on host-based internal download auth (not device provider)")
{
    // Real Net attaches auth to same-host downloads for SDK tarball URLs. Non-Scorbitron
    // clients must still be treated as internal when the asset URL is on the API host;
    // see isInternalDownloadForAuth in net_util.
    DeviceInfo nonScorbit;
    nonScorbit.provider = "not_scorbitron";

    const std::string apiBase = "https://api.scorbit.io:443";
    CHECK(isInternalDownloadForAuth(
            "https://api.scorbit.io/v2/releases/scorbit_sdk-1.0.2-testarch_testabi.tgz", apiBase,
            nonScorbit));
}
