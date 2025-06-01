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

#include <scorbit_sdk/version.h>
#include <updater.h>
#include "trompeloeil_printer.h"

#include <catch2/catch_test_macros.hpp>
#include <trompeloeil.hpp>

using namespace scorbit;
using namespace scorbit::detail;
using namespace trompeloeil;

namespace {

class MockNetBase : public NetBase
{
public:
    AuthStatus status() const override { return AuthStatus::NotAuthenticated; };
    void sendHeartbeat() override { };
    void requestPairCode(StringCallback) override {};
    const std::string &getMachineUuid() const override
    {
        static std::string rv;
        return rv;
    };
    const std::string &getPairDeeplink() const override
    {
        static std::string rv;
        return rv;
    };
    const std::string &getClaimDeeplink(int) const override
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
    void requestTopScores(sb_score_t, StringCallback) override {};
    void requestUnpair(StringCallback) override {};
    void authenticate() override { };
    void sendGameData(const GameData &) override { };

    MAKE_MOCK4(sendInstalled,
               void(const std::string &, const std::string &, std::optional<bool>,
                    std::optional<std::string>),
               override);
    MAKE_MOCK3(download, void(StringCallback, const std::string &, const std::string &), override);

    void downloadBuffer(VectorCallback, const std::string &, size_t) override { };
    PlayerProfilesManager &playersManager() override { return m_playersManager; };

private:
    PlayerProfilesManager m_playersManager;
};

} // namespace

TEST_CASE("Updater")
{
    boost::json::object json = boost::json::parse(R"(
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
        )")
                                       .as_object();

    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet; // mockNet will be moved into Updater, so we keep the ref

    // Create Updater object with mocked NetBase
    Updater updater(*mockNet, false);

    SECTION("happy path")
    {
        REQUIRE_CALL(mockNetRef,
                     download(_, "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz", _))
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json);
    }

    SECTION("download error")
    {
        REQUIRE_CALL(mockNetRef,
                     download(_, "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz", _))
                .LR_SIDE_EFFECT(_1(Error::ApiError, "some_temp_file.tar.gz");)
                .TIMES(1);

        REQUIRE_CALL(mockNetRef,
                     sendInstalled(eq("sdk"), eq("1.0.1"), eq<std::optional<bool>>(false),
                                   eq<std::optional<std::string>>(
                                           "Updater: download failed: 4, some_temp_file.tar.gz")))
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json);
    }

    SECTION("incorrect version, can't find download")
    {
        json.at("sdk").as_object()["version"] = "1.0.0";
        REQUIRE_CALL(mockNetRef,
                     sendInstalled(eq("sdk"), eq(SCORBIT_SDK_VERSION),
                                   eq<std::optional<bool>>(false), ANY(std::optional<std::string>)))
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json);
    }

    SECTION("empty assets")
    {
        json.at("sdk").as_object()["assets_json"].as_array().clear();
        REQUIRE_CALL(mockNetRef, sendInstalled(eq("sdk"), eq(SCORBIT_SDK_VERSION),
                                               eq<std::optional<bool>>(false),
                                               eq<std::optional<std::string>>("Assets list empty")))
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json);
    }
}

// Make sure that if current version is x.y.z then it updates only by x.y.*
TEST_CASE("Updater major.minor version mismatch")
{
    boost::json::object json = boost::json::parse(R"(
            {
                "sdk": {
                    "version": "1.1.0",
                    "assets_json": [
                        "https://example.com/scorbit_sdk-1.1.0-testarch_testabi.tgz"
                    ]
                }
            }
        )")
                                       .as_object();

    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet;

    // Create Updater object with mocked NetBase
    Updater updater(*mockNet, false);

    REQUIRE_CALL(mockNetRef,
                 sendInstalled(eq("sdk"), eq(SCORBIT_SDK_VERSION), eq<std::optional<bool>>(false),
                               eq<std::optional<std::string>>(
                                       "Version mismatch: can only update by 1.0.x, found: 1.1.0")))
            .TIMES(1);

    updater.checkNewVersionAndUpdate(json);
}

// Make sure that if using encrypted key, sdk key hash and prod key hash should match
TEST_CASE("Updater prod key hash check")
{
    boost::json::object json = boost::json::parse(R"(
            {
                "sdk": {
                    "version": "1.0.2",
                    "assets_json": [
                        "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz"
                    ]
                }
            }
        )")
                                       .as_object();

    auto mockNet = std::make_unique<MockNetBase>();
    auto &mockNetRef = *mockNet;

    SECTION("Mismatch while using encrypted key")
    {
        // Create Updater object with mocked NetBase
        Updater updater(*mockNet, true);

        REQUIRE_CALL(mockNetRef,
                     sendInstalled(eq("sdk"), eq(SCORBIT_SDK_VERSION),
                                   eq<std::optional<bool>>(false),
                                   eq<std::optional<std::string>>(
                                           "Using encrypted key, production key hash mismatch: "
                                           "expected unknown1, found unknown2")))
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json);
    }

    SECTION("No problem with mismatch while not using encrypted key")
    {
        // Create Updater object with mocked NetBase
        Updater updater(*mockNet, false);

        REQUIRE_CALL(mockNetRef,
                     download(_, "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz", _))
                .TIMES(1);

        updater.checkNewVersionAndUpdate(json);
    }
}
