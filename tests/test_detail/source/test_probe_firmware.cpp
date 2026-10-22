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

    /// Ends the last captured PATCH as the request would have.
    void finishLast(bool sent) { scorbitronUpdateDone(patches.back(), sent); }

    /// What the last captured PATCH would send if its request ran now.
    json lastWhenRun() const { return currentScorbitronFields(patches.back()); }

    std::vector<json> patches;
};

/// The entry @p firmware should become; @p bootReason is the name expected for its byte.
json entry(const ProbeFirmware &firmware, const char *bootReason = "unknown")
{
    return {{"timestamp_utc", firmware.timestampUtc},
            {"uid", firmware.uid},
            {"boot_reason", bootReason},
            {"watchdog_count", firmware.watchdogCount}};
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
    SECTION("a build beyond a signed 64-bit value is a bad read, reported as unknown")
    {
        read.probe.timestampUtc = uint64_t {1} << 63;
        CHECK(nfcProbeFirmwareEntry(read) == entry({0, 0xdeadbeef}));
        read.probe.timestampUtc = (uint64_t {1} << 63) - 1; // the largest the API stores
        CHECK(nfcProbeFirmwareEntry(read) == entry({(uint64_t {1} << 63) - 1, 0xdeadbeef}));
    }
    SECTION("a failed read reports nothing, leaving the API's record alone")
    {
        read.status = nfc::NfcProbeInfoResult::Status::ReadFailed;
        CHECK_FALSE(nfcProbeFirmwareEntry(read).has_value());
    }
}

TEST_CASE("A probe entry names its boot reason and carries its watchdog count")
{
    SECTION("each BootReason byte by its hardware-probe name")
    {
        const char *names[] {"unknown",      "power_on_reset", "bootloader_restart",
                             "soft_restart", "watchdog_timer", "watchdog_timer_acknowledged"};
        for (uint8_t byte = 0; byte < 6; ++byte) {
            INFO("byte " << int {byte});
            const ProbeFirmware firmware {1760000000, 9, byte, 3};
            CHECK(probeFirmwareEntry(firmware) == entry(firmware, names[byte]));
        }
    }
    SECTION("a byte beyond the known reasons is unknown")
    {
        const ProbeFirmware firmware {1760000000, 9, 200, 3};
        CHECK(probeFirmwareEntry(firmware).at("boot_reason") == "unknown");
    }
    SECTION("the pad's read carries its boot info")
    {
        nfc::ProbeInformation probe;
        probe.timestampUtc = 1760000000;
        probe.uid = 9;
        probe.bootReason = 4;
        probe.watchdogCount = 7;
        CHECK(nfcProbeFirmware(probe) == ProbeFirmware {1760000000, 9, 4, 7});
    }
    SECTION("legacy firmware reports no boot info: unknown and 0, as for its build")
    {
        nfc::ProbeInformation probe;
        probe.timestampHeader = 1;
        probe.timestampUtc = 1760000000;
        probe.uid = 9;
        probe.bootReason = 4;
        probe.watchdogCount = 7;
        CHECK(nfcProbeFirmware(probe) == ProbeFirmware {0, 9, 0, 0});
    }
}

TEST_CASE("The probe firmware config setter takes the host's probes only")
{
    const auto config = sb_config_create();
    REQUIRE(config);

    sb_config_set_probe_firmware(config, "cpu", true, CPU_BUILD.timestampUtc, CPU_BUILD.uid, 4,
                                 2); // watchdog_timer, twice
    sb_config_set_probe_firmware(config, "dmd", false, 123, 456, 1, 1);
    sb_config_set_probe_firmware(config, "nfc", true, 1, 2, 0, 0);   // the SDK's own
    sb_config_set_probe_firmware(config, "spike", true, 1, 2, 0, 0); // not a probe
    sb_config_set_probe_firmware(config, nullptr, true, 1, 2, 0, 0);
    sb_config_set_probe_firmware(nullptr, "cpu", true, 1, 2, 0, 0);

    REQUIRE(config->probeFirmware.size() == 2);
    CHECK(config->probeFirmware.at("cpu")
          == ProbeFirmware {CPU_BUILD.timestampUtc, CPU_BUILD.uid, 4, 2});
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

TEST_CASE("A probe that resets is reported, and an unchanged report sends nothing")
{
    DeviceInfo info;
    info.probeFirmware["cpu"] = CPU_BUILD;
    CapturingNet net(info);
    NetTestAccess::scorbitronObject(net);
    auto pad = nfcPad(1761000000, 0xcafe);
    pad->info.bootReason = 1; // power_on_reset
    net.setProbesManager(std::make_shared<nfc::ProbesManager>(pad));
    NetTestAccess::setStatus(net, AuthStatus::AuthenticatedPaired);

    SECTION("the pad's periodic check sends its new boot reason and count once")
    {
        NetTestAccess::checkNfcBootReason(net); // nothing changed since startup
        CHECK(net.patches.empty());

        pad->info.bootReason = 4; // watchdog_timer
        pad->info.watchdogCount = 1;
        NetTestAccess::checkNfcBootReason(net);
        REQUIRE(net.patches.size() == 1);
        CHECK(net.patches[0]
              == json {{"probe_firmware",
                        {{"nfc", entry({1761000000, 0xcafe, 4, 1}, "watchdog_timer")}}}});

        NetTestAccess::checkNfcBootReason(net);
        CHECK(net.patches.size() == 1);
    }
    SECTION("the host's probe: a repeated report sends nothing, a reset sends once")
    {
        net.setProbeFirmware("cpu", CPU_BUILD);
        CHECK(net.patches.empty());

        const ProbeFirmware reset {CPU_BUILD.timestampUtc, CPU_BUILD.uid, 4, 1};
        net.setProbeFirmware("cpu", reset);
        net.setProbeFirmware("cpu", reset);
        REQUIRE(net.patches.size() == 1);
        CHECK(net.patches[0]
              == json {{"probe_firmware", {{"cpu", entry(reset, "watchdog_timer")}}}});
    }
}

TEST_CASE("A change whose PATCH failed is sent again by a report once its backoff passed")
{
    DeviceInfo info;
    info.probeFirmware["cpu"] = CPU_BUILD;
    CapturingNet net(info);
    NetTestAccess::scorbitronObject(net);
    NetTestAccess::setStatus(net, AuthStatus::AuthenticatedPaired);
    const ProbeFirmware reset {CPU_BUILD.timestampUtc, CPU_BUILD.uid, 4, 1};

    SECTION("a probe's entry")
    {
        net.setProbeFirmware("cpu", reset);
        REQUIRE(net.patches.size() == 1);
        net.finishLast(false);
        net.setProbeFirmware("cpu", reset); // too soon after the failure
        CHECK(net.patches.size() == 1);

        NetTestAccess::expireScorbitronRetries(net);
        net.setProbeFirmware("cpu", reset); // the same report: resent, the API never got it
        REQUIRE(net.patches.size() == 2);
        CHECK(net.patches[1] == net.patches[0]);
        net.finishLast(true);

        net.setProbeFirmware("cpu", reset); // now the API has it
        CHECK(net.patches.size() == 2);
    }
    SECTION("the pbspk*comm firmware ID")
    {
        net.setPbspkcommFirmwareId("0a1b2c3d");
        net.finishLast(false);
        NetTestAccess::expireScorbitronRetries(net);
        net.setPbspkcommFirmwareId("0a1b2c3d");
        CHECK(net.patches.size() == 2);
    }
    SECTION("a failure marks only the probes it carried")
    {
        net.setProbeFirmware("cpu", reset);
        net.finishLast(false);
        net.setProbeFirmware("dmd", ProbeFirmware {1762000000, 7, 1, 0});
        net.finishLast(true);
        net.setProbeFirmware("dmd", ProbeFirmware {1762000000, 7, 1, 0}); // sent, unchanged
        CHECK(net.patches.size() == 2);
        NetTestAccess::expireScorbitronRetries(net);
        net.setProbeFirmware("cpu", reset); // still owed
        CHECK(net.patches.size() == 3);
    }
}

TEST_CASE("The pad's periodic check retries a failed PATCH with a growing backoff, one at a time")
{
    using namespace std::chrono_literals;
    DeviceInfo info;
    CapturingNet net(info);
    NetTestAccess::scorbitronObject(net);
    auto pad = nfcPad(1761000000, 0xcafe);
    pad->info.bootReason = 1; // power_on_reset
    net.setProbesManager(std::make_shared<nfc::ProbesManager>(pad));
    NetTestAccess::setStatus(net, AuthStatus::AuthenticatedPaired);

    pad->info.bootReason = 4; // watchdog_timer
    pad->info.watchdogCount = 1;
    NetTestAccess::checkNfcBootReason(net);
    REQUIRE(net.patches.size() == 1);
    net.finishLast(false);
    CHECK(NetTestAccess::scorbitronRetryBackoff(net, "probe_firmware/nfc") == 30s);

    NetTestAccess::checkNfcBootReason(net); // the next check, before the backoff passed
    CHECK(net.patches.size() == 1);

    NetTestAccess::expireScorbitronRetries(net);
    NetTestAccess::checkNfcBootReason(net);
    REQUIRE(net.patches.size() == 2);
    NetTestAccess::expireScorbitronRetries(net); // the retry is still in flight
    NetTestAccess::checkNfcBootReason(net);
    CHECK(net.patches.size() == 2);

    net.finishLast(false);
    CHECK(NetTestAccess::scorbitronRetryBackoff(net, "probe_firmware/nfc") == 60s);
    for (int i = 0; i < 6; ++i) {
        NetTestAccess::expireScorbitronRetries(net);
        NetTestAccess::checkNfcBootReason(net);
        net.finishLast(false);
    }
    CHECK(NetTestAccess::scorbitronRetryBackoff(net, "probe_firmware/nfc") == 10min);

    const auto sent = net.patches.size();
    pad->info.watchdogCount = 2; // a new reset is news: sent despite the backoff
    NetTestAccess::checkNfcBootReason(net);
    REQUIRE(net.patches.size() == sent + 1);
    net.finishLast(true);
    CHECK_FALSE(NetTestAccess::scorbitronRetryBackoff(net, "probe_firmware/nfc"));
    NetTestAccess::checkNfcBootReason(net);
    CHECK(net.patches.size() == sent + 1);
}

TEST_CASE("A change left to the full object is owed again if the full object fails")
{
    DeviceInfo info;
    info.probeFirmware["cpu"] = CPU_BUILD;
    CapturingNet net(info);
    NetTestAccess::scorbitronObject(net);
    const ProbeFirmware reset {CPU_BUILD.timestampUtc, CPU_BUILD.uid, 4, 1};

    net.setProbeFirmware("cpu", reset); // not yet authenticated: stored for the full object
    REQUIRE(net.patches.empty());

    SECTION("the full object fails: the next report after the backoff sends it")
    {
        NetTestAccess::scorbitronObjectSendDone(net, false);
        NetTestAccess::setStatus(net, AuthStatus::AuthenticatedPaired);
        net.setProbeFirmware("cpu", reset); // too soon
        CHECK(net.patches.empty());
        NetTestAccess::expireScorbitronRetries(net);
        net.setProbeFirmware("cpu", reset);
        REQUIRE(net.patches.size() == 1);
        CHECK(net.patches[0]
              == json {{"probe_firmware", {{"cpu", entry(reset, "watchdog_timer")}}}});
    }
    SECTION("the full object succeeds: nothing is owed")
    {
        NetTestAccess::scorbitronObjectSendDone(net, true);
        NetTestAccess::setStatus(net, AuthStatus::AuthenticatedPaired);
        NetTestAccess::expireScorbitronRetries(net);
        net.setProbeFirmware("cpu", reset);
        CHECK(net.patches.empty());
    }
}

TEST_CASE("A PATCH that waited out a lost authentication sends the latest value")
{
    DeviceInfo info;
    info.probeFirmware["cpu"] = CPU_BUILD;
    CapturingNet net(info);
    NetTestAccess::scorbitronObject(net);
    NetTestAccess::setStatus(net, AuthStatus::AuthenticatedPaired);
    const ProbeFirmware reset {CPU_BUILD.timestampUtc, CPU_BUILD.uid, 4, 1};
    const ProbeFirmware again {CPU_BUILD.timestampUtc, CPU_BUILD.uid, 4, 2};

    SECTION("a probe's entry")
    {
        net.setProbeFirmware("cpu", reset);
        REQUIRE(net.patches.size() == 1);
        NetTestAccess::setStatus(net, AuthStatus::NotAuthenticated);
        net.setProbeFirmware("cpu", again); // stored only; the full object will carry it
        REQUIRE(net.patches.size() == 1);
        CHECK(net.lastWhenRun()
              == json {{"probe_firmware", {{"cpu", entry(again, "watchdog_timer")}}}});
    }
    SECTION("the pbspk*comm firmware ID")
    {
        net.setPbspkcommFirmwareId("0a1b2c3d");
        NetTestAccess::setStatus(net, AuthStatus::NotAuthenticated);
        net.setPbspkcommFirmwareId("4e5f6a7b");
        CHECK(net.lastWhenRun() == json {{"pbspkcomm_firmware_id", "4e5f6a7b"}});
    }
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
