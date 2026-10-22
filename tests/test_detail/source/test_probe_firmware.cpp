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

// SB-5757 / SB-5752: installed probe firmware rides on the Scorbitron object, at startup and
// again whenever it changes.
//
// Every Net here is built from a DeviceInfo that does not validate, so its worker never starts;
// the PATCH a change would send is captured instead.

#include "device_info.h"
#include "net.h"
#include "net_util.h"
#include <nfc/probes_manager.h>
#include <scorbit_sdk/config_c.h>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace scorbit;
using namespace scorbit::detail;
using nlohmann::json;

namespace {

constexpr ProbeFirmware CPU_BUILD {1760000000, 0x11223344};
constexpr ProbeFirmware NEWER_CPU_BUILD {1765000000, 0x11223344};

/// An NFC pad whose identity read reports @ref info, or fails.
class FakeNfcProbe : public nfc::NfcProbeDevice
{
public:
    nfc::ProbeInformation info;
    bool failRead {false};

    bool getInformations(nfc::ProbeInformation &out) override
    {
        out = info;
        return !failRead;
    }
    bool getNfcInformations(nfc::NfcInformation &) override { return true; }
    bool setUri(const std::string &) override { return true; }
    bool setLedsBrightness(uint8_t) override { return true; }
    bool setType(nfc::NfcType) override { return true; }
};

std::shared_ptr<FakeNfcProbe> nfcPad(uint64_t timestampUtc, uint32_t uid)
{
    auto pad = std::make_shared<FakeNfcProbe>();
    pad->info.timestampUtc = timestampUtc;
    pad->info.uid = uid;
    return pad;
}

/// Records every Scorbitron PATCH body instead of sending it.
struct CapturingNet : Net {
    explicit CapturingNet(DeviceInfo info = {})
        : Net(std::move(info), {})
    {
    }

    void sendScorbitronUpdate(std::string body, std::string) override
    {
        patches.push_back(json::parse(body));
    }

    std::vector<json> patches;
};

json entry(const ProbeFirmware &firmware)
{
    return {{"timestamp_utc", firmware.timestampUtc}, {"uid", firmware.uid}};
}

} // namespace

TEST_CASE("A probe read becomes a probe_firmware entry")
{
    nfc::NfcProbeInfoResult read;
    read.status = nfc::NfcProbeInfoResult::Status::Ok;
    read.probe.timestampUtc = 1760000000;
    read.probe.uid = 0xdeadbeef;

    SECTION("a current pad reports its build and UID")
    {
        CHECK(nfcProbeFirmwareEntry(read) == entry({1760000000, 0xdeadbeef}));
    }
    SECTION("legacy firmware reports no build, so it is 0")
    {
        read.probe.timestampHeader = 1;
        CHECK(nfcProbeFirmwareEntry(read) == entry({0, 0xdeadbeef}));
    }
    SECTION("no pad attached is null, so the API marks it absent")
    {
        read.status = nfc::NfcProbeInfoResult::Status::NotPresent;
        CHECK(nfcProbeFirmwareEntry(read) == json(nullptr));
    }
    SECTION("a failed read reports nothing, leaving the API's record alone")
    {
        read.status = nfc::NfcProbeInfoResult::Status::ReadFailed;
        CHECK_FALSE(nfcProbeFirmwareEntry(read).has_value());
    }
}

TEST_CASE("The probe firmware config setter takes the host's probes only")
{
    const auto config = sb_config_create();
    REQUIRE(config);

    sb_config_set_probe_firmware(config, "cpu", true, CPU_BUILD.timestampUtc, CPU_BUILD.uid);
    sb_config_set_probe_firmware(config, "dmd", false, 123, 456);
    sb_config_set_probe_firmware(config, "nfc", true, 1, 2);   // the SDK's own
    sb_config_set_probe_firmware(config, "spike", true, 1, 2); // not a probe
    sb_config_set_probe_firmware(config, nullptr, true, 1, 2);
    sb_config_set_probe_firmware(nullptr, "cpu", true, 1, 2);

    REQUIRE(config->probeFirmware.size() == 2);
    CHECK(config->probeFirmware.at("cpu") == CPU_BUILD);
    CHECK_FALSE(config->probeFirmware.at("dmd").has_value());
    sb_config_destroy(config);
}

TEST_CASE("The Scorbitron object carries the probes known at startup")
{
    SECTION("the host's probes as set, attached or not")
    {
        DeviceInfo info;
        info.probeFirmware["cpu"] = CPU_BUILD;
        info.probeFirmware["dmd"] = std::nullopt;
        CapturingNet net(info);
        const auto object = NetTestAccess::scorbitronObject(net);
        CHECK(object.at("probe_firmware") == json {{"cpu", entry(CPU_BUILD)}, {"dmd", nullptr}});
    }
    SECTION("nothing set: no probe_firmware at all")
    {
        CapturingNet net;
        CHECK_FALSE(NetTestAccess::scorbitronObject(net).contains("probe_firmware"));
    }
}

TEST_CASE("The SDK reports the NFC pad itself")
{
    DeviceInfo info;
    info.probeFirmware["cpu"] = CPU_BUILD;
    CapturingNet net(info);
    NetTestAccess::scorbitronObject(net); // as at construction

    SECTION("an attached pad: its build and UID, next to the host's probes")
    {
        NetTestAccess::setProbesManager(
                net, std::make_shared<nfc::ProbesManager>(nfcPad(1761000000, 0xcafe)));
        net.refreshNfcFirmware();
        const auto object = NetTestAccess::currentScorbitronObject(net);
        CHECK(object.at("probe_firmware").at("nfc") == entry({1761000000, 0xcafe}));
        CHECK(object.at("probe_firmware").at("cpu") == entry(CPU_BUILD));
    }
    SECTION("no pad: null")
    {
        NetTestAccess::setProbesManager(net, std::make_shared<nfc::ProbesManager>(nullptr));
        net.refreshNfcFirmware();
        CHECK(NetTestAccess::currentScorbitronObject(net).at("probe_firmware").at("nfc")
              == json(nullptr));
    }
    SECTION("a failed read: no nfc entry")
    {
        auto pad = nfcPad(1761000000, 0xcafe);
        pad->failRead = true;
        NetTestAccess::setProbesManager(net, std::make_shared<nfc::ProbesManager>(pad));
        net.refreshNfcFirmware();
        CHECK_FALSE(
                NetTestAccess::currentScorbitronObject(net).at("probe_firmware").contains("nfc"));
    }
    SECTION("no probe manager, as on a host that does not use the pad: nothing")
    {
        net.refreshNfcFirmware();
        CHECK_FALSE(
                NetTestAccess::currentScorbitronObject(net).at("probe_firmware").contains("nfc"));
    }
    CHECK(net.patches.empty()); // not authenticated: the first send carries it
}

TEST_CASE("Installing the probe manager at startup reads the NFC pad")
{
    CapturingNet net;
    NetTestAccess::scorbitronObject(net);
    net.setProbesManager(std::make_shared<nfc::ProbesManager>(nfcPad(1761000000, 0xcafe)));
    CHECK(NetTestAccess::currentScorbitronObject(net).at("probe_firmware").at("nfc")
          == entry({1761000000, 0xcafe}));
}

TEST_CASE("A probe flashed after startup is reported without a restart")
{
    DeviceInfo info;
    info.probeFirmware["cpu"] = CPU_BUILD;
    info.probeFirmware["dmd"] = std::nullopt;
    CapturingNet net(info);
    NetTestAccess::scorbitronObject(net);

    SECTION("before authentication: the object changes, the first send carries it")
    {
        net.setProbeFirmware("cpu", NEWER_CPU_BUILD);
        CHECK(net.patches.empty());
        const auto object = NetTestAccess::currentScorbitronObject(net);
        CHECK(object.at("probe_firmware").at("cpu") == entry(NEWER_CPU_BUILD));
        CHECK(object.at("probe_firmware").at("dmd") == json(nullptr)); // the others stay
    }
    SECTION("once authenticated: the change alone is sent at once")
    {
        NetTestAccess::setStatus(net, AuthStatus::AuthenticatedPaired);

        net.setProbeFirmware("cpu", NEWER_CPU_BUILD);
        REQUIRE(net.patches.size() == 1);
        CHECK(net.patches[0] == json {{"probe_firmware", {{"cpu", entry(NEWER_CPU_BUILD)}}}});

        net.setProbeFirmware("dmd", ProbeFirmware {1762000000, 7});
        REQUIRE(net.patches.size() == 2);
        CHECK(net.patches[1] == json {{"probe_firmware", {{"dmd", entry({1762000000, 7})}}}});

        net.setProbeFirmware("cpu", std::nullopt); // unplugged
        REQUIRE(net.patches.size() == 3);
        CHECK(net.patches[2] == json {{"probe_firmware", {{"cpu", nullptr}}}});
    }
    SECTION("while pairing is checked: sent too, since the full object may already be on its way")
    {
        NetTestAccess::setStatus(net, AuthStatus::AuthenticatedCheckingPairing);
        net.setProbeFirmware("cpu", NEWER_CPU_BUILD);
        REQUIRE(net.patches.size() == 1);
        CHECK(net.patches[0] == json {{"probe_firmware", {{"cpu", entry(NEWER_CPU_BUILD)}}}});
    }
    SECTION("while authenticating: not sent, the full object's send after it carries the change")
    {
        NetTestAccess::setStatus(net, AuthStatus::Authenticating);
        net.setProbeFirmware("cpu", NEWER_CPU_BUILD);
        CHECK(net.patches.empty());
    }
    SECTION("the NFC pad, re-read after a flash")
    {
        auto pad = nfcPad(1761000000, 0xcafe);
        NetTestAccess::setProbesManager(net, std::make_shared<nfc::ProbesManager>(pad));
        NetTestAccess::setStatus(net, AuthStatus::AuthenticatedUnpaired);

        pad->info.timestampUtc = 1766000000; // the new build
        net.refreshNfcFirmware();
        REQUIRE(net.patches.size() == 1);
        CHECK(net.patches[0] == json {{"probe_firmware", {{"nfc", entry({1766000000, 0xcafe})}}}});
    }
    SECTION("a host cannot report the NFC pad or an unknown probe")
    {
        NetTestAccess::setStatus(net, AuthStatus::AuthenticatedPaired);
        net.setProbeFirmware("nfc", NEWER_CPU_BUILD);
        net.setProbeFirmware("spike", NEWER_CPU_BUILD);
        CHECK(net.patches.empty());
        CHECK_FALSE(
                NetTestAccess::currentScorbitronObject(net).at("probe_firmware").contains("nfc"));
    }
}

TEST_CASE("A replaced pbspk*comm's firmware ID is reported without a restart")
{
    DeviceInfo info;
    info.pbspkcommFirmwareId = "11111111";
    CapturingNet net(info);
    NetTestAccess::scorbitronObject(net);
    NetTestAccess::setStatus(net, AuthStatus::AuthenticatedPaired);

    net.setPbspkcommFirmwareId("0a1b2c3d");
    REQUIRE(net.patches.size() == 1);
    CHECK(net.patches[0] == json {{"pbspkcomm_firmware_id", "0a1b2c3d"}});
    CHECK(NetTestAccess::currentScorbitronObject(net).at("pbspkcomm_firmware_id") == "0a1b2c3d");

    net.setPbspkcommFirmwareId(""); // nothing to report
    CHECK(net.patches.size() == 1);
}
