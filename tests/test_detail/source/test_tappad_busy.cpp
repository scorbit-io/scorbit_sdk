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

// A tap pad held by another process is busy, not gone: nothing may forget the chip, rediscover it
// (which writes probes into the holder's sequence) or settle for a weaker key because of it.
//
// Every TPM open below happens while another thread holds the tap pad lock, so it gives up before
// opening any device: these tests never probe real hardware, even with a tap pad attached.

#include <catch2/catch_test_macros.hpp>

#include "device_info.h"
#include "key_resolver.h"
#include "net.h"
#include "nfc_tpm_key_resolver.h"
#include "tappad_lock_test_path.h"

#include <tpm/hardwaretpm.h>
#include <tpm/tappad_lock.h>
#include <tpm/tpm.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using namespace scorbit;
using namespace scorbit::detail;

struct HardwareTpmTestAccess {
    static void setDevice(HardwareTpm &tpm, const TpmDevice &device)
    {
        const std::lock_guard<std::mutex> lock {tpm.m_deviceMutex};
        tpm.m_device = device;
    }
    static TpmDevice device(const HardwareTpm &tpm)
    {
        const std::lock_guard<std::mutex> lock {tpm.m_deviceMutex};
        return tpm.m_device;
    }
    static bool readIdentity(HardwareTpm &tpm) { return tpm.readIdentity(); }
    static void setBusy(HardwareTpm &tpm, bool busy)
    {
        const std::lock_guard<std::mutex> lock {tpm.m_deviceMutex};
        tpm.m_busy = busy;
    }
    static void noteSignBusy(const HardwareTpm &tpm, const Tpm &device)
    {
        tpm.noteSignBusy(device);
    }
};

namespace {

enum class Outcome { Resolve, Defer, Fail };

// A key resolver whose answer the test sets, counting how often it is asked.
class FakeResolver : public IKeyResolver
{
public:
    FakeResolver(std::shared_ptr<Outcome> outcome, std::shared_ptr<int> calls)
        : m_outcome(std::move(outcome))
        , m_calls(std::move(calls))
    {
    }
    bool tryResolve(DeviceInfo &, const std::string &) override
    {
        ++*m_calls;
        return *m_outcome == Outcome::Resolve;
    }
    bool shouldRetryLater() const override { return *m_outcome == Outcome::Defer; }
    SignerCallback createSigner() const override { return {}; }

private:
    std::shared_ptr<Outcome> m_outcome;
    std::shared_ptr<int> m_calls;
};

struct Chain {
    std::shared_ptr<Outcome> first = std::make_shared<Outcome>(Outcome::Defer);
    std::shared_ptr<int> firstCalls = std::make_shared<int>(0);
    std::shared_ptr<int> secondCalls = std::make_shared<int>(0);

    std::vector<std::unique_ptr<IKeyResolver>> resolvers() const
    {
        std::vector<std::unique_ptr<IKeyResolver>> list;
        list.push_back(std::make_unique<FakeResolver>(first, firstCalls));
        list.push_back(std::make_unique<FakeResolver>(std::make_shared<Outcome>(Outcome::Fail),
                                                      secondCalls));
        return list;
    }
};

constexpr auto TIMESTAMP = "1773013652";

} // namespace

TEST_CASE("Key resolution waits for a busy resolver instead of falling through", "[tappad_busy]")
{
    Chain chain;
    Net net {DeviceInfo {}, chain.resolvers()};

    CHECK(NetTestAccess::resolveKeys(net, TIMESTAMP) == KeyResolution::Deferred);
    CHECK(*chain.firstCalls == 1);
    CHECK(*chain.secondCalls == 0);
    // Kept, so the next authentication attempt asks again.
    CHECK(NetTestAccess::keyResolverCount(net) == 2);
}

TEST_CASE("Key resolution falls through a resolver that has simply failed", "[tappad_busy]")
{
    Chain chain;
    *chain.first = Outcome::Fail;
    Net net {DeviceInfo {}, chain.resolvers()};

    CHECK(NetTestAccess::resolveKeys(net, TIMESTAMP) == KeyResolution::Failed);
    CHECK(*chain.firstCalls == 1);
    CHECK(*chain.secondCalls == 1);
    CHECK(NetTestAccess::keyResolverCount(net) == 0);
}

// The test Net's worker never starts (its DeviceInfo does not validate), so a retry timer armed
// below can never fire a network call; each test still disarms it.

TEST_CASE("A deferred authentication does not report AuthenticationFailed", "[tappad_busy]")
{
    Chain chain;
    Net net {DeviceInfo {}, chain.resolvers()};

    // As the authentication task leaves it before resolving keys.
    NetTestAccess::setStatus(net, AuthStatus::Authenticating);
    NetTestAccess::deferAuthentication(net);
    CHECK(NetTestAccess::status(net) == AuthStatus::NotAuthenticated);

    NetTestAccess::setStatus(net, AuthStatus::Authenticating);
    NetTestAccess::deferAuthentication(net);
    CHECK(NetTestAccess::status(net) == AuthStatus::NotAuthenticated);
    NetTestAccess::cancelAuthRetry(net);
}

TEST_CASE("A busy signature defers a first authentication instead of failing it", "[tappad_busy]")
{
    Net net {DeviceInfo {}, {}};
    NetTestAccess::setSignerBusyCheck(net, [] { return true; });

    NetTestAccess::setStatus(net, AuthStatus::Authenticating);
    NetTestAccess::onEmptySignature(net, true);
    CHECK(NetTestAccess::status(net) == AuthStatus::NotAuthenticated);
    NetTestAccess::cancelAuthRetry(net);
}

TEST_CASE("A busy signature during a token refresh keeps the device authenticated", "[tappad_busy]")
{
    Net net {DeviceInfo {}, {}};
    NetTestAccess::setSignerBusyCheck(net, [] { return true; });

    NetTestAccess::setStatus(net, AuthStatus::AuthenticatedPaired);
    NetTestAccess::onEmptySignature(net, false);
    CHECK(NetTestAccess::status(net) == AuthStatus::AuthenticatedPaired);
    NetTestAccess::cancelAuthRetry(net);
}

TEST_CASE("A signature that is empty for another reason still fails the authentication",
          "[tappad_busy]")
{
    Net net {DeviceInfo {}, {}};
    NetTestAccess::setSignerBusyCheck(net, [] { return false; });

    NetTestAccess::setStatus(net, AuthStatus::Authenticating);
    NetTestAccess::onEmptySignature(net, true);
    CHECK(NetTestAccess::status(net) == AuthStatus::AuthenticationFailed);
    NetTestAccess::cancelAuthRetry(net);
}

TEST_CASE("A long deferral escalates once, and a resolution resets it", "[tappad_busy]")
{
    Chain chain;
    Net net {DeviceInfo {}, chain.resolvers()};
    const auto t0 = std::chrono::steady_clock::now();

    CHECK_FALSE(NetTestAccess::noteKeyResolutionDeferred(net, t0));
    CHECK_FALSE(NetTestAccess::noteKeyResolutionDeferred(net, t0 + 4min));
    CHECK(NetTestAccess::noteKeyResolutionDeferred(net, t0 + 5min));
    CHECK_FALSE(NetTestAccess::noteKeyResolutionDeferred(net, t0 + 6min));
    CHECK_FALSE(NetTestAccess::noteKeyResolutionDeferred(net, t0 + 60min));

    *chain.first = Outcome::Resolve;
    REQUIRE(NetTestAccess::resolveKeys(net, TIMESTAMP) == KeyResolution::Resolved);

    // A new run of deferrals starts its own clock and escalates again.
    const auto t1 = t0 + 61min;
    CHECK_FALSE(NetTestAccess::noteKeyResolutionDeferred(net, t1));
    CHECK_FALSE(NetTestAccess::noteKeyResolutionDeferred(net, t1 + 4min));
    CHECK(NetTestAccess::noteKeyResolutionDeferred(net, t1 + 5min));
}

#if defined(SCORBIT_TAPPAD_LOCK)

using tappad_lock_test::OtherHolder;

namespace {

const TpmDevice NOWHERE {TpmBus::USB, 0, "/dev/nonexistent-tappad-tty"};

} // namespace

TEST_CASE("TPM discovery does not scan while the tap pad is held", "[tappad_busy]")
{
    int scans = 0;
    const auto scan = [&] {
        ++scans;
        return std::string {"/dev/ttyACM9"};
    };

    {
        OtherHolder other;
        REQUIRE(other.held());
        bool busy = false;
        CHECK(scanUnderTappadLock(scan, 50, &busy).empty());
        CHECK(busy);
        CHECK(scans == 0);
    }

    bool busy = true;
    CHECK(scanUnderTappadLock(scan, 50, &busy) == "/dev/ttyACM9");
    CHECK_FALSE(busy);
    CHECK(scans == 1);
}

TEST_CASE("NfcTpmKeyResolver defers when discovery finds the tap pad busy", "[tappad_busy]")
{
    int scans = 0;
    NfcTpmKeyResolver resolver {[&]() -> TpmLocation {
        bool busy = false;
        auto path = scanUnderTappadLock(
                [&] {
                    ++scans;
                    return std::string {};
                },
                50, &busy);
        return busy ? TpmLocation::busyTapPad() : TpmLocation {path};
    }};

    OtherHolder other;
    REQUIRE(other.held());
    DeviceInfo info;
    CHECK_FALSE(resolver.tryResolve(info, TIMESTAMP));
    CHECK(resolver.shouldRetryLater());
    CHECK(scans == 0);
}

TEST_CASE("NfcTpmKeyResolver defers when the TPM open finds the tap pad busy", "[tappad_busy]")
{
    std::atomic<int> discoveries {0};
    NfcTpmKeyResolver resolver {[&]() -> TpmLocation {
        ++discoveries;
        return NOWHERE.usbDevicePath;
    }};

    OtherHolder other;
    REQUIRE(other.held());
    DeviceInfo info;
    CHECK_FALSE(resolver.tryResolve(info, TIMESTAMP));
    CHECK(resolver.shouldRetryLater());
    CHECK(info.uuid.empty());
    // Discovery ran once up front; the busy open did not go on to the locator.
    CHECK(discoveries == 1);
}

TEST_CASE("An I2C TPM never waits for the tap pad lock", "[tappad_busy]")
{
    OtherHolder other;
    REQUIRE(other.held());

    const auto start = std::chrono::steady_clock::now();
    // A bus that cannot exist, so no real I2C device is opened.
    const Tpm tpm {TpmDevice {TpmBus::I2C, 99, {}}};
    CHECK_FALSE(tpm.busy());
    CHECK(std::chrono::steady_clock::now() - start < 1s);
}

TEST_CASE("A busy tap pad keeps the cached TPM route and skips rediscovery", "[tappad_busy]")
{
    OtherHolder other;
    REQUIRE(other.held());

    std::atomic<int> locates {0};
    HardwareTpm hw {TpmBus::USB, NOWHERE.usbDevicePath, [&]() -> TpmLocation {
                        ++locates;
                        return {};
                    }};
    CHECK(hw.busy());
    CHECK(locates == 0);

    HardwareTpmTestAccess::setDevice(hw, NOWHERE);
    CHECK(hw.signDigest(ByteArray(32, 0)).empty());
    CHECK(hw.busy());
    CHECK(HardwareTpmTestAccess::device(hw).usbDevicePath == NOWHERE.usbDevicePath);
    CHECK(locates == 0);

    // An identity read that was only busy keeps the route too.
    CHECK_FALSE(HardwareTpmTestAccess::readIdentity(hw));
    CHECK(HardwareTpmTestAccess::device(hw).isValid());
}

TEST_CASE("A sign that finds the tap pad busy after a clean open marks the TPM busy",
          "[tappad_busy]")
{
    OtherHolder other;
    REQUIRE(other.held());

    HardwareTpm hw {TpmBus::USB, NOWHERE.usbDevicePath, [] { return TpmLocation {}; }};
    // As after an open that got the lock: not busy yet.
    HardwareTpmTestAccess::setBusy(hw, false);
    REQUIRE_FALSE(hw.busy());

    // The device a sign ran on, whose retry timed out on the lock.
    const Tpm signedOn {NOWHERE, [](uint64_t, const ByteArray &) { return true; }};
    REQUIRE(signedOn.busy());

    HardwareTpmTestAccess::noteSignBusy(hw, signedOn);
    CHECK(hw.busy());
}

TEST_CASE("A locator that finds the tap pad busy marks the TPM busy", "[tappad_busy]")
{
    // The open is not held off by the lock here: it fails on a tty that does not exist, then
    // Tpm's own USB fallbacks run, so this only runs where no probe or tap pad is attached.
    if (tappad_lock_test::tapPadMayBePresent()) {
        SKIP("a probe or tap pad is attached; this test must not open it");
    }

    std::atomic<int> locates {0};
    const HardwareTpm hw {TpmBus::USB, NOWHERE.usbDevicePath, [&] {
                              ++locates;
                              return TpmLocation::busyTapPad();
                          }};
    CHECK(locates == 1);
    // The constructor's identity read must not overwrite the locator's busy.
    CHECK(hw.busy());
}

#endif
