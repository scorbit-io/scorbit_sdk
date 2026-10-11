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

#include <nfc/nfc_probe_device.h>
#include <nfc/probes_manager.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <thread>

// clazy:excludeall=non-pod-global-static

using Status = nfc::NfcProbeInfoResult::Status;

namespace {

/**
 * Fake probe that models the hardware tap indicator: getNfcInformations() returns and clears it.
 * Flags any two calls that overlap in time, which would mean ProbesManager failed to serialize.
 */
class FakeNfcProbeDevice : public nfc::NfcProbeDevice
{
public:
    bool getInformations(nfc::ProbeInformation &info) override
    {
        InFlight guard {*this};
        if (blockNextInformations.exchange(false)) {
            // Hold the caller (and so the ProbesManager lock) until the test releases it
            enteredBlock.set_value();
            releaseBlock.wait();
        }
        if (failProbeRead || failAll) {
            return false;
        }
        info = probe;
        return true;
    }

    bool getNfcInformations(nfc::NfcInformation &info) override
    {
        InFlight guard {*this};
        if (failAll) {
            return false;
        }
        info = nfc;
        info.lastId = tapIndicator.exchange(0);
        return true;
    }

    bool setUri(const std::string &uri) override
    {
        InFlight guard {*this};
        lastUri = uri;
        return true;
    }

    bool setLedsBrightness(uint8_t brightness) override
    {
        InFlight guard {*this};
        ++ledWrites;
        if (failLeds) {
            return false;
        }
        lastBrightness = brightness;
        return true;
    }

    bool setType(nfc::NfcType) override
    {
        InFlight guard {*this};
        return true;
    }

    nfc::ProbeInformation probe;
    nfc::NfcInformation nfc;
    std::atomic<uint32_t> tapIndicator {0};
    std::atomic<bool> failAll {false};
    std::atomic<bool> failProbeRead {false};
    std::atomic<bool> overlapped {false};
    std::string lastUri;
    std::atomic<int> lastBrightness {-1};
    std::atomic<int> ledWrites {0};
    std::atomic<bool> failLeds {false};
    std::atomic<bool> blockNextInformations {false};
    std::promise<void> enteredBlock;
    std::promise<void> releaseBlockPromise;
    std::shared_future<void> releaseBlock {releaseBlockPromise.get_future().share()};

private:
    struct InFlight {
        explicit InFlight(FakeNfcProbeDevice &device)
            : m_device {device}
        {
            if (m_device.m_inFlight.fetch_add(1) != 0) {
                m_device.overlapped = true;
            }
            // Widen the window so an unserialized caller would be caught
            std::this_thread::yield();
        }
        ~InFlight() { m_device.m_inFlight.fetch_sub(1); }

        FakeNfcProbeDevice &m_device;
    };

    std::atomic<int> m_inFlight {0};
};

nfc::NfcProbeInfoResult okResult()
{
    nfc::NfcProbeInfoResult result;
    result.status = Status::Ok;
    result.probe.id = "NFC";
    result.probe.uid = 0xDEADBEEF;
    result.nfc.capacitance = 77;
    result.nfc.tagUri = "https://example.com/t/abc";
    return result;
}

} // namespace

TEST_CASE("ProbesManager reports not present without an NFC probe", "[nfc][ProbesManager]")
{
    SECTION("default constructed")
    {
        nfc::ProbesManager manager;
        CHECK_FALSE(manager.hasNfc());
        CHECK(manager.nfcProbeInfo().status == Status::NotPresent);
        CHECK_FALSE(manager.isNfcTagRead());
        CHECK_FALSE(manager.setNfcTag("x"));
        CHECK_FALSE(manager.setNfcLeds(nfc::NfcLedMode::Idle));
        CHECK_FALSE(manager.probesBootReason(nfc::ProbeType::NFC).has_value());
    }

    SECTION("null device")
    {
        nfc::ProbesManager manager {nullptr};
        CHECK_FALSE(manager.hasNfc());
        CHECK(manager.nfcProbeInfo().status == Status::NotPresent);
    }
}

TEST_CASE("ProbesManager nfcProbeInfo returns probe and NFC fields", "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    device->probe = okResult().probe;
    device->nfc = okResult().nfc;
    nfc::ProbesManager manager {device};

    REQUIRE(manager.hasNfc());
    const auto info = manager.nfcProbeInfo();
    REQUIRE(info.status == Status::Ok);
    CHECK(info.probe.id == "NFC");
    CHECK(info.probe.uid == 0xDEADBEEF);
    CHECK(info.nfc.capacitance == 77);
    CHECK(info.nfc.tagUri == "https://example.com/t/abc");
    CHECK(info.nfc.lastId == 0);
    CHECK_FALSE(manager.isNfcTagRead());
}

TEST_CASE("ProbesManager keeps a tap consumed by nfcProbeInfo for isNfcTagRead",
          "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    nfc::ProbesManager manager {device};

    device->tapIndicator = 42; // tap lands just before the info read

    const auto info = manager.nfcProbeInfo();
    REQUIRE(info.status == Status::Ok);
    CHECK(info.nfc.lastId == 42);
    CHECK(device->tapIndicator.load() == 0); // the read cleared the hardware indicator

    CHECK(manager.isNfcTagRead());
    CHECK_FALSE(manager.isNfcTagRead());
}

TEST_CASE("ProbesManager reads the pad on a poll even with a tap queued", "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    nfc::ProbesManager manager {device};

    device->tapIndicator = 1; // first tap, consumed by an info read and queued
    REQUIRE(manager.nfcProbeInfo().status == Status::Ok);

    device->tapIndicator = 2; // second tap reaches the pad before the next poll
    CHECK(manager.isNfcTagRead());
    CHECK(device->tapIndicator.load() == 0); // that poll still read and cleared the pad

    device->tapIndicator = 3; // a third tap cannot overwrite an unread second one
    CHECK(manager.isNfcTagRead());
    CHECK(manager.isNfcTagRead());
    CHECK_FALSE(manager.isNfcTagRead());
}

TEST_CASE("ProbesManager reports read failures", "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    nfc::ProbesManager manager {device};

    SECTION("whole read fails")
    {
        device->failAll = true;
        CHECK(manager.nfcProbeInfo().status == Status::ReadFailed);
        CHECK_FALSE(manager.isNfcTagRead());
    }

    SECTION("probe read fails after an NFC read that saw a tap")
    {
        device->failProbeRead = true;
        device->tapIndicator = 7;
        CHECK(manager.nfcProbeInfo().status == Status::ReadFailed);
        CHECK(manager.isNfcTagRead()); // the tap is still delivered
        CHECK_FALSE(manager.isNfcTagRead());
    }
}

TEST_CASE("ProbesManager probesBootReason names WatchdogTimerAcknowledged", "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    device->probe.id = "NFC";
    device->probe.bootReason = 5;
    device->probe.watchdogCount = 2;
    nfc::ProbesManager manager {device};

    const auto reason = manager.probesBootReason(nfc::ProbeType::NFC);
    REQUIRE(reason.has_value());
    CHECK(reason->find("WatchdogTimerAcknowledged") != std::string::npos);
    CHECK(reason->find("watchdog count: 2") != std::string::npos);
}

TEST_CASE("ProbesManager keeps a tap read during a boot-reason rescan", "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    int scans = 0;
    // Rescan finds no new probe, so the current device stays and gets the TAG-mode read
    nfc::ProbesManager manager {device, [&scans](nfc::probe_t, const nfc::ProbeDisplayCallback &) {
                                    ++scans;
                                    return std::shared_ptr<nfc::NfcProbeDevice> {};
                                }};

    device->failProbeRead = true; // getInformations fails -> probesBootReason rescans
    device->tapIndicator = 9;     // a tap is waiting in the hardware

    const auto reason = manager.probesBootReason(nfc::ProbeType::NFC);
    REQUIRE(reason.has_value());
    CHECK(reason->find("couldn't get probe info") != std::string::npos);
    CHECK(scans == 1);
    CHECK(device->tapIndicator.load() == 0); // the rescan's TAG-mode read cleared it

    CHECK(manager.isNfcTagRead()); // ...but the tap is still delivered
    CHECK_FALSE(manager.isNfcTagRead());
}

TEST_CASE("ProbesManager adopts a probe found by a rescan", "[nfc][ProbesManager]")
{
    auto oldDevice = std::make_shared<FakeNfcProbeDevice>();
    auto newDevice = std::make_shared<FakeNfcProbeDevice>();
    newDevice->probe.id = "NFC";
    newDevice->probe.bootReason = 3;
    nfc::ProbesManager manager {oldDevice,
                                [newDevice](nfc::probe_t, const nfc::ProbeDisplayCallback &) {
                                    return std::shared_ptr<nfc::NfcProbeDevice> {newDevice};
                                }};

    oldDevice->failProbeRead = true;
    (void)manager.probesBootReason(nfc::ProbeType::NFC);

    const auto reason = manager.probesBootReason(nfc::ProbeType::NFC);
    REQUIRE(reason.has_value());
    CHECK(reason->find("SoftRestart") != std::string::npos);
}

TEST_CASE("ProbesManager rescanNfc adopts a probe attached after start", "[nfc][ProbesManager]")
{
    auto lateDevice = std::make_shared<FakeNfcProbeDevice>();
    lateDevice->probe.id = "NFC";
    lateDevice->tapIndicator = 8; // a tap waiting on the newly found pad
    int scans = 0;
    nfc::ProbesManager manager {nullptr,
                                [&](nfc::probe_t, const nfc::ProbeDisplayCallback &) {
                                    ++scans;
                                    return std::shared_ptr<nfc::NfcProbeDevice> {lateDevice};
                                }};
    REQUIRE(manager.nfcProbeInfo().status == Status::NotPresent);

    manager.rescanNfc();

    CHECK(scans == 1);
    CHECK(manager.hasNfc());
    CHECK(manager.nfcProbeInfo().status == Status::Ok);
    // Unlike the startup enumeration, a forced rescan keeps the tap it read
    CHECK(manager.isNfcTagRead());
    CHECK_FALSE(manager.isNfcTagRead());

    // The discovery guard leaves the injected path working across repeated scans
    manager.rescanNfc();
    CHECK(scans == 2);
    CHECK(manager.hasNfc());
}

TEST_CASE("ProbesManager rescanNfc keeps the current probe when none is found",
          "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    device->probe.id = "NFC";
    device->probe.bootReason = 3;
    nfc::ProbesManager manager {device, [](nfc::probe_t, const nfc::ProbeDisplayCallback &) {
                                    return std::shared_ptr<nfc::NfcProbeDevice> {};
                                }};

    manager.rescanNfc();

    REQUIRE(manager.hasNfc());
    const auto info = manager.nfcProbeInfo();
    REQUIRE(info.status == Status::Ok);
    CHECK(info.probe.bootReason == 3);
}

TEST_CASE("ProbesManager drops a stale tap at startup enumeration", "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    device->tapIndicator = 5; // tap left over from before the SDK started
    nfc::ProbesManager manager {nullptr, [device](nfc::probe_t, const nfc::ProbeDisplayCallback &) {
                                    return std::shared_ptr<nfc::NfcProbeDevice> {device};
                                }};

    manager.enumerate(nfc::ProbeType::NFC);

    REQUIRE(manager.hasNfc());
    CHECK(device->tapIndicator.load() == 0);
    CHECK_FALSE(manager.isNfcTagRead());
}

TEST_CASE("ProbesManager setNfcLeds never blocks on a busy probe", "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    device->probe.id = "NFC";
    nfc::ProbesManager manager {device};

    // Another thread holds the probe lock inside a probe read until released
    device->blockNextInformations = true;
    std::thread holder([&] { (void)manager.probesBootReason(nfc::ProbeType::NFC); });
    device->enteredBlock.get_future().wait();

    // Returns at once (try-lock), queues the mode; the latest of two wins
    CHECK(manager.setNfcLeds(nfc::NfcLedMode::GameSession));
    CHECK(manager.setNfcLeds(nfc::NfcLedMode::Idle));
    CHECK(device->ledWrites == 0);

    device->releaseBlockPromise.set_value();
    holder.join();

    (void)manager.isNfcTagRead(); // the next poll applies the queued mode
    CHECK(device->ledWrites == 1);
    CHECK(device->lastBrightness == 30); // Idle

    (void)manager.isNfcTagRead(); // already applied: not re-sent
    CHECK(device->ledWrites == 1);
}

TEST_CASE("ProbesManager poll skips without blocking while the probe is busy",
          "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    device->probe.id = "NFC";
    nfc::ProbesManager manager {device};

    device->blockNextInformations = true;
    std::thread holder([&] { (void)manager.probesBootReason(nfc::ProbeType::NFC); });
    device->enteredBlock.get_future().wait();

    device->tapIndicator = 11; // a tap lands while the probe is busy
    CHECK_FALSE(manager.isNfcTagRead()); // skipped: returns at once without reading
    CHECK(device->tapIndicator.load() == 11);

    device->releaseBlockPromise.set_value();
    holder.join();

    CHECK(manager.isNfcTagRead()); // the next poll still delivers the tap
    CHECK_FALSE(manager.isNfcTagRead());
}

TEST_CASE("ProbesManager rejects an invalid LED mode", "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    nfc::ProbesManager manager {device};

    CHECK_FALSE(manager.setNfcLeds(static_cast<nfc::NfcLedMode>(99)));
    (void)manager.isNfcTagRead();
    CHECK(device->ledWrites == 0);
}

TEST_CASE("ProbesManager retries a failed LED write on the next poll", "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();
    nfc::ProbesManager manager {device};

    device->failLeds = true;
    CHECK(manager.setNfcLeds(nfc::NfcLedMode::GameSession)); // queued for retry
    CHECK(device->ledWrites == 1);

    (void)manager.isNfcTagRead(); // still failing: retried
    CHECK(device->ledWrites == 2);

    device->failLeds = false;
    (void)manager.isNfcTagRead(); // succeeds
    CHECK(device->ledWrites == 3);
    CHECK(device->lastBrightness == 0); // GameSession

    (void)manager.isNfcTagRead();
    CHECK(device->ledWrites == 3);

    // A mode change applies immediately when the probe is free
    CHECK(manager.setNfcLeds(nfc::NfcLedMode::Idle));
    CHECK(device->ledWrites == 4);
    CHECK(device->lastBrightness == 30);
}

TEST_CASE("ProbesManager setNfcLeds without a probe reports false", "[nfc][ProbesManager]")
{
    nfc::ProbesManager manager {nullptr};
    CHECK_FALSE(manager.setNfcLeds(nfc::NfcLedMode::Idle));
}

TEST_CASE("ProbesManager serializes probe I/O and loses no taps under concurrency",
          "[nfc][ProbesManager]")
{
    constexpr int TAPS = 200;
    auto device = std::make_shared<FakeNfcProbeDevice>();
    nfc::ProbesManager manager {device};

    std::atomic<bool> stopPolling {false};
    std::atomic<bool> stopInfo {false};
    std::atomic<int> tapsSeen {0};
    std::thread poller;
    std::thread infoReader;

    // Stops and joins both threads on every exit path: a joinable std::thread destructor aborts
    struct StopAndJoin {
        std::atomic<bool> &stopInfo;
        std::thread &infoReader;
        std::atomic<bool> &stopPolling;
        std::thread &poller;

        void operator()()
        {
            stopInfo = true;
            if (infoReader.joinable()) {
                infoReader.join();
            }
            stopPolling = true;
            if (poller.joinable()) {
                poller.join();
            }
        }
        ~StopAndJoin() { (*this)(); }
    } stopAndJoin {stopInfo, infoReader, stopPolling, poller};

    // The SDK's own side: poll, program a new tag on every tap, toggle LEDs
    poller = std::thread([&] {
        while (!stopPolling) {
            if (manager.isNfcTagRead()) {
                ++tapsSeen;
                manager.setNfcTag("https://example.com/t/next");
            }
            manager.setNfcLeds(nfc::NfcLedMode::GameSession);
        }
    });

    // The host application reading probe info at the same time
    infoReader = std::thread([&] {
        while (!stopInfo) {
            (void)manager.nfcProbeInfo();
        }
    });

    // One tap at a time: the hardware holds a single indicator
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    for (uint32_t tap = 1; tap <= TAPS; ++tap) {
        device->tapIndicator = tap;
        while (device->tapIndicator != 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
    }
    const bool allTapsRead = device->tapIndicator.load() == 0;

    stopAndJoin();

    // A tap consumed by the last info read may still be pending
    int drained = 0;
    while (drained <= TAPS && manager.isNfcTagRead()) {
        ++drained;
    }

    CHECK(allTapsRead);
    CHECK_FALSE(device->overlapped.load());
    CHECK(tapsSeen + drained == TAPS);
}

TEST_CASE("ProbesManager records the last tap seen by any read", "[nfc][ProbesManager]")
{
    auto device = std::make_shared<FakeNfcProbeDevice>();

    SECTION("none seen")
    {
        nfc::ProbesManager manager {device};
        const auto info = manager.nfcProbeInfo();
        REQUIRE(info.status == Status::Ok);
        CHECK(info.lastSeenTapId == 0);
        CHECK(info.lastSeenTapUnixMs == 0);
    }

    SECTION("seen by a poll, persists, and a later tap replaces it")
    {
        nfc::ProbesManager manager {device};
        device->tapIndicator = 21;
        CHECK(manager.isNfcTagRead());

        auto info = manager.nfcProbeInfo();
        CHECK(info.nfc.lastId == 0); // the indicator is already cleared
        CHECK(info.lastSeenTapId == 21);
        const auto firstSeenMs = info.lastSeenTapUnixMs;
        CHECK(firstSeenMs > 0);

        info = manager.nfcProbeInfo(); // persists across further reads
        CHECK(info.lastSeenTapId == 21);
        CHECK(info.lastSeenTapUnixMs == firstSeenMs);

        device->tapIndicator = 22;
        CHECK(manager.isNfcTagRead());
        info = manager.nfcProbeInfo();
        CHECK(info.lastSeenTapId == 22);
        CHECK(info.lastSeenTapUnixMs >= firstSeenMs); // same millisecond is possible
    }

    SECTION("seen by an info read")
    {
        nfc::ProbesManager manager {device};
        device->tapIndicator = 31;
        const auto info = manager.nfcProbeInfo();
        CHECK(info.nfc.lastId == 31);
        CHECK(info.lastSeenTapId == 31);
        CHECK(info.lastSeenTapUnixMs > 0);
    }

    SECTION("seen by the startup read, which still drops it as a tap")
    {
        device->tapIndicator = 41;
        nfc::ProbesManager manager {nullptr,
                                    [device](nfc::probe_t, const nfc::ProbeDisplayCallback &) {
                                        return std::shared_ptr<nfc::NfcProbeDevice> {device};
                                    }};
        manager.enumerate(nfc::ProbeType::NFC);
        CHECK_FALSE(manager.isNfcTagRead());

        const auto info = manager.nfcProbeInfo();
        CHECK(info.lastSeenTapId == 41);
        CHECK(info.lastSeenTapUnixMs > 0);
    }
}
