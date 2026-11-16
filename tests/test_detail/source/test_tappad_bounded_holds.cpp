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

// A tap pad that stops answering must not hold the lock for more than a few seconds (SB-6638).
// Each test makes one chip call under the hold atcaRetry takes, over a fake transport: a
// pseudo-terminal for the native tty, and the test hooks of the HID HAL and the libusb CDC open.

#include <catch2/catch_test_macros.hpp>

#include "atca_retry.h"
#include "tappad_lock_test_path.h"

#include <tpm/tappad_lock.h>

#if defined(SCORBIT_TAPPAD_LOCK) && defined(ATCA_TAPPAD_LOCK_ENV_OVERRIDE)
#    include <cryptoauthlib.h>
#    include <hal/atca_hal.h>
#    include <hal/hal_bounded_io.h>
#    if defined(ATCA_HAL_KIT_UART_LIBUSB)
#        include <hal/hal_linux_cdc_libusb.h>
#    endif

#    include <algorithm>
#    include <atomic>
#    include <cerrno>
#    include <chrono>
#    include <cstdio>
#    include <cstring>
#    include <functional>
#    include <future>
#    include <mutex>
#    include <string>
#    include <thread>
#    include <vector>

#    include <fcntl.h>
#    include <poll.h>
#    include <stdlib.h>
#    include <unistd.h>

using namespace std::chrono_literals;

namespace {

using Clock = std::chrono::steady_clock;

// calib polls a failed receive 1250 times, 2 ms apart (ATCA_POLLING_MAX_TIME_MSEC), and at most a
// 0.5 s read and a 1 s write timeout come on top: a dead pad's hold ends in 3-5 s. Margin for slow
// sleeps on a loaded host; without the bounds a hold lasts minutes or forever.
constexpr auto HOLD_BOUND = 8s;

struct HoldResult {
    ATCA_STATUS status;
    Clock::duration elapsed;
    bool inBound;
};

// Runs call under the hold atcaRetry takes for one attempt. If it is still running at HOLD_BOUND,
// unblock makes the fake transport fail so that the call returns and the test can report it.
HoldResult runHeld(const std::function<ATCA_STATUS()> &call, const std::function<void()> &unblock)
{
    const auto start = Clock::now();
    auto done = std::async(std::launch::async, [&] {
        std::chrono::milliseconds waited {0};
        return atca_retry::attemptLocked(call, TappadLock::DEFAULT_TIMEOUT_MS, waited);
    });
    const bool inBound = done.wait_for(HOLD_BOUND) == std::future_status::ready;
    if (!inBound) {
        unblock();
    }
    const ATCA_STATUS status = done.get();
    return {status, Clock::now() - start, inBound};
}

// Another thread gets the lock: the call left no HAL hold behind on its thread.
bool lockFreeForOthers()
{
    return std::async(std::launch::async,
                      [] {
                          const TappadLock hold {500};
                          return hold.ok();
                      })
            .get();
}

// The kit-protocol device calib talks to, on the transport of type, already awake so the command
// goes straight out and calib polls for its reply.
struct KitDevice {
    ATCAIfaceCfg cfg {};
    atca_device device {};

    explicit KitDevice(ATCAIfaceType type, const std::string &path = {})
        : m_path(path)
    {
        cfg.iface_type = type;
        cfg.devtype = ATECC508A;
        if (type == ATCA_UART_IFACE) {
            cfg.atcauart.dev_interface = ATCA_KIT_AUTO_IFACE;
            cfg.atcauart.baud = 115200;
            cfg.atcauart.wordsize = 8;
            cfg.atcauart.parity = 2;
            cfg.atcauart.stopbits = 1;
            cfg.cfg_data = m_path.data();
        } else {
            cfg.atcahid.packetsize = 64;
        }
        REQUIRE(hal_iface_init(&cfg, &device.mIface.hal, &device.mIface.phy) == ATCA_SUCCESS);
        device.mIface.mIfaceCFG = &cfg;
        device.device_state = ATCA_DEVICE_STATE_ACTIVE;
    }
    ~KitDevice()
    {
        if (m_opened) {
            device.mIface.phy->halrelease(device.mIface.hal_data);
        }
    }
    KitDevice(const KitDevice &) = delete;
    KitDevice &operator=(const KitDevice &) = delete;

    ATCA_STATUS open()
    {
        const ATCA_STATUS status = device.mIface.phy->halinit(&device.mIface, &cfg);
        m_opened = status == ATCA_SUCCESS;
        return status;
    }

    ATCA_STATUS info(uint8_t *revision) { return calib_info(&device, revision); }

private:
    std::string m_path;
    bool m_opened = false;
};

constexpr uint8_t REVISION[4] = {0x00, 0x00, 0x50, 0x00};

// The kit reply to an Info command: status 00 and the packet count, revision and CRC, in hex.
std::string infoReply()
{
    uint8_t packet[7] = {7, REVISION[0], REVISION[1], REVISION[2], REVISION[3], 0, 0};
    atCRC(5, packet, &packet[5]);
    std::string reply = "00(";
    for (const uint8_t byte : packet) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02X", byte);
        reply += hex;
    }
    return reply + ")\n";
}

// A pseudo-terminal stands in for the pad's tty; the test plays the pad on its master side.
struct Pty {
    int master = -1;
    std::string slave;

    Pty()
    {
        master = ::posix_openpt(O_RDWR | O_NOCTTY);
        REQUIRE(master >= 0);
        REQUIRE(::grantpt(master) == 0);
        REQUIRE(::unlockpt(master) == 0);
        slave = ::ptsname(master);
    }
    ~Pty() { closeMaster(); }
    Pty(const Pty &) = delete;
    Pty &operator=(const Pty &) = delete;

    // The pad goes away: the HAL's reads and writes on the slave fail at once.
    void closeMaster()
    {
        if (master >= 0) {
            ::close(master);
            master = -1;
        }
    }
};

// A pseudo-terminal has no modem lines to raise.
int noModemLines(int, int)
{
    return 0;
}

struct FakeModemLines {
    FakeModemLines() { hal_uart_test_set_modem_bits(noModemLines); }
    ~FakeModemLines() { hal_uart_test_set_modem_bits(nullptr); }
};

namespace fake_hid {

std::atomic<bool> released {false};
std::mutex mutex;
std::string pending;

// A pad that takes every report and never answers. A negative timeout waits until the test
// releases it, as hid_read() would wait forever.
int silentRead(hid_device_ *, unsigned char *, size_t, int timeoutMs)
{
    const auto end = timeoutMs < 0 ? Clock::time_point::max()
                                   : Clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!released && Clock::now() < end) {
        std::this_thread::sleep_for(1ms);
    }
    return released ? -1 : 0;
}

int acceptWrite(hid_device_ *, const unsigned char *, size_t length)
{
    return static_cast<int>(length);
}

// A pad that answers every command at once: Info with a revision, anything else with "00()".
int answeringWrite(hid_device_ *, const unsigned char *data, size_t length)
{
    const std::string command(reinterpret_cast<const char *>(data) + 1, length - 1);
    const std::lock_guard<std::mutex> lock(mutex);
    pending = command.find(":t(") != std::string::npos ? infoReply() : std::string("00()\n");
    return static_cast<int>(length);
}

int answeringRead(hid_device_ *, unsigned char *data, size_t length, int)
{
    const std::lock_guard<std::mutex> lock(mutex);
    if (pending.empty()) {
        return 0;
    }
    std::memset(data, 0, length);
    std::memcpy(data, pending.data(), std::min(length, pending.size()));
    pending.clear();
    return static_cast<int>(std::min<size_t>(length, 64));
}

struct Io {
    Io(int (*read)(hid_device_ *, unsigned char *, size_t, int),
       int (*write)(hid_device_ *, const unsigned char *, size_t))
    {
        released = false;
        pending.clear();
        hal_kit_hid_test_set_io(read, write);
    }
    ~Io() { hal_kit_hid_test_set_io(nullptr, nullptr); }
};

} // namespace fake_hid

} // namespace

TEST_CASE("Tap pad hold: a tty that never answers ends within the bound", "[tappad_bounded]")
{
    const FakeModemLines modem;
    Pty pty;
    KitDevice kit(ATCA_UART_IFACE, pty.slave);
    REQUIRE(kit.open() == ATCA_SUCCESS);

    uint8_t revision[4] = {};
    const auto result = runHeld([&] { return kit.info(revision); }, [&] { pty.closeMaster(); });

    CHECK(result.inBound);
    CHECK(result.status != ATCA_SUCCESS);
    CHECK(lockFreeForOthers());
}

TEST_CASE("Tap pad hold: a tty that takes no writes ends within the bound", "[tappad_bounded]")
{
    const FakeModemLines modem;
    Pty pty;
    KitDevice kit(ATCA_UART_IFACE, pty.slave);
    REQUIRE(kit.open() == ATCA_SUCCESS);

    // Fill the tty's output, which the pad never drains.
    const int filler = ::open(pty.slave.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    REQUIRE(filler >= 0);
    const std::vector<char> block(4096, 'x');
    while (::write(filler, block.data(), block.size()) > 0) { }
    REQUIRE(errno == EAGAIN);

    uint8_t revision[4] = {};
    const auto result = runHeld([&] { return kit.info(revision); }, [&] { pty.closeMaster(); });
    ::close(filler);

    CHECK(result.inBound);
    CHECK(result.status != ATCA_SUCCESS);
    CHECK(lockFreeForOthers());
}

TEST_CASE("Tap pad hold: a tty streaming NUL bytes ends within the bound", "[tappad_bounded]")
{
    const FakeModemLines modem;
    Pty pty;
    KitDevice kit(ATCA_UART_IFACE, pty.slave);
    REQUIRE(kit.open() == ATCA_SUCCESS);
    REQUIRE(::fcntl(pty.master, F_SETFL, O_NONBLOCK) == 0);

    std::atomic<bool> stop {false};
    std::thread pad([&] {
        const std::vector<char> nuls(4096, '\0');
        while (!stop) {
            if (::write(pty.master, nuls.data(), nuls.size()) < 0) {
                std::this_thread::sleep_for(1ms);
            }
        }
    });

    uint8_t revision[4] = {};
    const auto result = runHeld([&] { return kit.info(revision); },
                                [&] {
                                    stop = true;
                                    pad.join();
                                    pty.closeMaster();
                                });
    stop = true;
    if (pad.joinable()) {
        pad.join();
    }

    CHECK(result.inBound);
    CHECK(result.status != ATCA_SUCCESS);
    CHECK(lockFreeForOthers());
}

TEST_CASE("Tap pad hold: a healthy tty pad answers as before", "[tappad_bounded]")
{
    const FakeModemLines modem;
    Pty pty;
    KitDevice kit(ATCA_UART_IFACE, pty.slave);
    REQUIRE(kit.open() == ATCA_SUCCESS);

    std::atomic<bool> stop {false};
    std::atomic<bool> replied {true};
    std::string commands;
    std::thread pad([&] {
        std::string line;
        char byte = 0;
        pollfd pfd {pty.master, POLLIN, 0};
        while (!stop) {
            if (::poll(&pfd, 1, 10) != 1 || ::read(pty.master, &byte, 1) != 1) {
                continue;
            }
            line += byte;
            if (byte == '\n') {
                const std::string reply =
                        line.find(":t(") != std::string::npos ? infoReply() : std::string("00()\n");
                replied = replied
                       && ::write(pty.master, reply.data(), reply.size())
                                  == static_cast<ssize_t>(reply.size());
                commands += line;
                line.clear();
            }
        }
    });

    uint8_t revision[4] = {};
    const auto result = runHeld([&] { return kit.info(revision); }, [&] { pty.closeMaster(); });
    stop = true;
    pad.join();

    CHECK(result.status == ATCA_SUCCESS);
    CHECK(result.elapsed < 1s);
    CHECK(replied);
    CHECK(std::memcmp(revision, REVISION, sizeof(REVISION)) == 0);
    // The Info command, then the idle, byte for byte as the kit protocol frames them.
    CHECK(commands == "E:t(0730000000035D)\nE:i()\n");
}

TEST_CASE("Tap pad hold: a HID pad that never answers ends within the bound", "[tappad_bounded]")
{
    const fake_hid::Io io(fake_hid::silentRead, fake_hid::acceptWrite);
    KitDevice kit(ATCA_HID_IFACE);
    int device = 0;
    kit.device.mIface.hal_data = &device;

    uint8_t revision[4] = {};
    const auto result =
            runHeld([&] { return kit.info(revision); }, [] { fake_hid::released = true; });

    CHECK(result.inBound);
    CHECK(result.status != ATCA_SUCCESS);
    CHECK(lockFreeForOthers());
}

TEST_CASE("Tap pad hold: a healthy HID pad answers as before", "[tappad_bounded]")
{
    const fake_hid::Io io(fake_hid::answeringRead, fake_hid::answeringWrite);
    KitDevice kit(ATCA_HID_IFACE);
    int device = 0;
    kit.device.mIface.hal_data = &device;

    uint8_t revision[4] = {};
    const auto result =
            runHeld([&] { return kit.info(revision); }, [] { fake_hid::released = true; });

    CHECK(result.status == ATCA_SUCCESS);
    CHECK(result.elapsed < 1s);
    CHECK(std::memcmp(revision, REVISION, sizeof(REVISION)) == 0);
}

#    if defined(ATCA_HAL_KIT_UART_LIBUSB)

namespace fake_cdc {

std::atomic<int> opens {0};
std::atomic<bool> released {false};

// The first open (init) leaves the transport closed, as a failed transfer does. Every later one
// stands for reopening a silent pad and the 0.5 s read that then times out.
ATCA_STATUS open(const char *, uint32_t, atca_cdc_libusb_t **cdc)
{
    *cdc = nullptr;
    if (opens++ == 0) {
        return ATCA_SUCCESS;
    }
    const auto end = Clock::now() + 500ms;
    while (!released && Clock::now() < end) {
        std::this_thread::sleep_for(1ms);
    }
    return ATCA_COMM_FAIL;
}

} // namespace fake_cdc

TEST_CASE("Tap pad hold: libusb CDC receives do not reopen the pad", "[tappad_bounded]")
{
    fake_cdc::opens = 0;
    fake_cdc::released = false;
    hal_uart_test_set_cdc_open(fake_cdc::open);
    KitDevice kit(ATCA_UART_IFACE, "usb:001:002:1");
    REQUIRE(kit.open() == ATCA_SUCCESS);

    // calib's response poll: a receive every 2 ms for 2.5 s until one succeeds.
    const auto poll = [&] {
        ATCA_STATUS status = ATCA_SUCCESS;
        for (uint32_t i = 0; i <= ATCA_POLLING_MAX_TIME_MSEC / ATCA_POLLING_FREQUENCY_TIME_MSEC;
             ++i) {
            uint8_t reply[16] = {};
            uint16_t length = sizeof(reply);
            if ((status = atreceive(&kit.device.mIface, 0, reply, &length)) == ATCA_SUCCESS) {
                break;
            }
            atca_delay_ms(ATCA_POLLING_FREQUENCY_TIME_MSEC);
        }
        return status;
    };
    const auto result = runHeld(poll, [] { fake_cdc::released = true; });

    CHECK(result.inBound);
    CHECK(result.status != ATCA_SUCCESS);
    CHECK(fake_cdc::opens == 1);
    CHECK(lockFreeForOthers());

    // The next send reopens it.
    uint8_t byte = '\n';
    CHECK(hal_uart_send(&kit.device.mIface, 0, &byte, 1) == ATCA_COMM_FAIL);
    CHECK(fake_cdc::opens == 2);
    hal_uart_test_set_cdc_open(nullptr);
}

#    endif

#endif
