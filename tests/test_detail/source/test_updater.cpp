/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Apr 2025
 *
 ****************************************************************************/

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
    Updater updater(*mockNet);

    SECTION("happy path")
    {
        ALLOW_CALL(mockNetRef,
                   download(_, "https://example.com/scorbit_sdk-1.0.2-testarch_testabi.tgz", _));

        updater.checkNewVersionAndUpdate(json);
    }

    SECTION("incorrect version, can't find download")
    {
        json.at("sdk").as_object()["version"] = "1.0.0";
        ALLOW_CALL(mockNetRef,
                   sendInstalled(eq("sdk"), eq(SCORBIT_SDK_VERSION), eq(std::optional<bool>(false)),
                                 ANY(std::optional<std::string>)));

        updater.checkNewVersionAndUpdate(json);
    }

    SECTION("empty assets")
    {
        json.at("sdk").as_object()["assets_json"].as_array().clear();
        ALLOW_CALL(mockNetRef,
                   sendInstalled(eq("sdk"), eq(SCORBIT_SDK_VERSION), eq(std::optional<bool>(false)),
                                 eq(std::optional<std::string>("Assets list empty"))));

        updater.checkNewVersionAndUpdate(json);
    }
}
