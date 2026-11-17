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

// A tap pad that stops answering must not hold the lock for more than a few seconds, and one that
// answers late must neither fail nor leave its reply for the next command (SB-6638). Each test
// makes chip calls under the hold atcaRetry takes, over a fake pad: behind a pseudo-terminal for
// the native tty, and behind the test hooks of the HID HAL and the libusb CDC transport.

#include <catch2/catch_test_macros.hpp>

#include "atca_retry.h"
#include "tappad_lock_test_path.h"

#include <tpm/tappad_lock.h>

#if defined(SCORBIT_TAPPAD_LOCK) && defined(ATCA_TAPPAD_LOCK_ENV_OVERRIDE)
#    include <cryptoauthlib.h>
#    include <hal/atca_hal.h>
#    include <hal/hal_bounded_io.h>
#    include <hal/kit_protocol.h>
#    if defined(ATCA_HAL_KIT_UART_LIBUSB)
#        include <hal/hal_linux_cdc_libusb.h>
#    endif

#    include <algorithm>
#    include <atomic>
#    include <cerrno>
#    include <chrono>
#    include <cstdio>
#    include <cstring>
#    include <deque>
#    include <functional>
#    include <future>
#    include <mutex>
#    include <optional>
#    include <string>
#    include <thread>
#    include <vector>

#    include <fcntl.h>
#    include <poll.h>
#    include <stdlib.h>
#    include <termios.h>
#    include <unistd.h>

using namespace std::chrono_literals;

namespace {

using Clock = std::chrono::steady_clock;
using Delay = std::optional<std::chrono::milliseconds>;

// Loose: the tests count transfers to tell bounded from unbounded. A dead pad's hold measures
// 5-8 s; without the bounds it lasts minutes or forever.
constexpr auto HOLD_BOUND = 20s;

// The Info command calib_info sends, as the kit protocol frames it.
const std::string INFO_COMMAND = "E:t(0730000000035D)\n";

struct HoldResult {
    ATCA_STATUS status;
    Clock::duration elapsed;
    bool inBound;
};

// Runs call under the hold atcaRetry takes for one attempt. If it is still running at HOLD_BOUND,
// unblock makes the fake pad fail so that the call returns and the test can report it.
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

// The kit reply to the n-th Info command: status 00, and a packet whose revision ends in n + 1.
std::string infoReply(int n)
{
    uint8_t packet[7] = {7, 0x00, 0x00, 0x50, static_cast<uint8_t>(n + 1), 0, 0};
    atCRC(5, packet, &packet[5]);
    std::string reply = "00(";
    for (const uint8_t byte : packet) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02X", byte);
        reply += hex;
    }
    return reply + ")\n";
}

// A tap pad's kit firmware: it answers each command line, Info after a delay chosen per command
// (none: never), anything else at once. A silent pad answers nothing.
class FakeKitPad
{
public:
    explicit FakeKitPad(std::function<Delay(int)> infoDelay = [](int) { return Delay {0ms}; })
        : m_infoDelay(std::move(infoDelay))
    {
    }
    struct Silent {
    };
    explicit FakeKitPad(Silent)
        : m_silent(true)
    {
    }

    void write(const char *data, size_t length)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < length; ++i) {
            if (data[i] == '\0') {
                continue; // HID reports are zero-padded
            }
            m_line += data[i];
            if (data[i] != '\n') {
                continue;
            }
            m_commands += m_line;
            if (m_line.find(":t(") == std::string::npos) {
                if (!m_silent) {
                    m_replies.push_back({Clock::now(), "00()\n"});
                }
            } else {
                const auto delay = m_silent ? Delay {} : m_infoDelay(m_infos);
                if (delay) {
                    m_replies.push_back({Clock::now() + *delay, infoReply(m_infos)});
                }
                ++m_infos;
            }
            m_line.clear();
        }
    }

    // Up to max bytes of a reply that is due, waiting up to timeoutMs (negative: until released).
    size_t read(char *out, size_t max, int timeoutMs)
    {
        if (timeoutMs > 1) {
            ++m_timedReads;
        }
        const auto end = timeoutMs < 0 ? Clock::time_point::max()
                                       : Clock::now() + std::chrono::milliseconds(timeoutMs);
        while (!m_released) {
            {
                const std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_replies.empty() && m_replies.front().due <= Clock::now()) {
                    auto &bytes = m_replies.front().bytes;
                    const size_t n = std::min(max, bytes.size());
                    std::memcpy(out, bytes.data(), n);
                    bytes.erase(0, n);
                    if (bytes.empty()) {
                        m_replies.pop_front();
                    }
                    return n;
                }
            }
            if (Clock::now() >= end) {
                break;
            }
            std::this_thread::sleep_for(1ms);
        }
        return 0;
    }

    void release() { m_released = true; }
    bool released() const { return m_released; }
    // Reads that waited for a reply rather than only draining what was queued.
    int timedReads() const { return m_timedReads; }
    std::string commands() const
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_commands;
    }

private:
    struct Reply {
        Clock::time_point due;
        std::string bytes;
    };

    std::function<Delay(int)> m_infoDelay;
    mutable std::mutex m_mutex;
    std::string m_line;
    std::string m_commands;
    std::deque<Reply> m_replies;
    int m_infos = 0;
    std::atomic<int> m_timedReads {0};
    std::atomic<bool> m_released {false};
    bool m_silent = false;
};

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

    // Info under the hold atcaRetry takes; revision receives the reply's.
    HoldResult info(uint8_t (&revision)[4], const std::function<void()> &unblock)
    {
        device.device_state = ATCA_DEVICE_STATE_ACTIVE;
        return runHeld([&] { return calib_info(&device, revision); }, unblock);
    }

private:
    std::string m_path;
    bool m_opened = false;
};

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

// Plays pad behind the master side of pty until destroyed.
class PtyPad
{
public:
    PtyPad(Pty &pty, FakeKitPad &pad)
        : m_thread([this, &pty, &pad] {
            char buffer[64];
            pollfd pfd {pty.master, POLLIN, 0};
            while (!m_stop) {
                if (::poll(&pfd, 1, 2) == 1) {
                    const ssize_t n = ::read(pty.master, buffer, sizeof(buffer));
                    if (n > 0) {
                        pad.write(buffer, static_cast<size_t>(n));
                    }
                }
                const size_t n = pad.read(buffer, sizeof(buffer), 0);
                if (n > 0 && ::write(pty.master, buffer, n) != static_cast<ssize_t>(n)) {
                    m_writeFailed = true;
                }
            }
        })
    {
    }
    ~PtyPad()
    {
        m_stop = true;
        m_thread.join();
    }
    PtyPad(const PtyPad &) = delete;
    PtyPad &operator=(const PtyPad &) = delete;

    bool writeFailed() const { return m_writeFailed; }

private:
    std::atomic<bool> m_stop {false};
    std::atomic<bool> m_writeFailed {false};
    std::thread m_thread;
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

constexpr uint8_t revisionOf(int n)
{
    return static_cast<uint8_t>(n + 1);
}

namespace fake_hid {

FakeKitPad *pad = nullptr;

int read(hid_device_ *, unsigned char *data, size_t length, int timeoutMs)
{
    const size_t n = pad->read(reinterpret_cast<char *>(data), length, timeoutMs);
    return pad->released() ? -1 : static_cast<int>(n);
}

int write(hid_device_ *, const unsigned char *data, size_t length)
{
    pad->write(reinterpret_cast<const char *>(data) + 1, length - 1);
    return static_cast<int>(length);
}

// A HID kit device whose reports go to and from padIn.
struct Device {
    explicit Device(FakeKitPad &padIn)
    {
        pad = &padIn;
        hal_kit_hid_test_set_io(read, write);
        kit.device.mIface.hal_data = &handle;
    }
    ~Device()
    {
        hal_kit_hid_test_set_io(nullptr, nullptr);
        pad = nullptr;
    }
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;

    int handle = 0; // never dereferenced: every transfer goes through the hooks
    KitDevice kit {ATCA_HID_IFACE};
};

} // namespace fake_hid

} // namespace

TEST_CASE("Tap pad hold: a tty that never answers ends within the bound", "[tappad_bounded]")
{
    const FakeModemLines modem;
    Pty pty;
    FakeKitPad pad(FakeKitPad::Silent {});
    const PtyPad player(pty, pad);
    KitDevice kit(ATCA_UART_IFACE, pty.slave);
    REQUIRE(kit.open() == ATCA_SUCCESS);

    uint8_t revision[4] = {};
    const auto result = kit.info(revision, [&] { pty.closeMaster(); });

    CHECK(result.inBound);
    CHECK(result.status != ATCA_SUCCESS);
    // The read failed and closed the tty: no retry and no idle followed the command.
    CHECK(pad.commands() == INFO_COMMAND);
    CHECK(lockFreeForOthers());
    // A device fault, which atcaRetry retries, not a bad call.
    uint8_t byte = '\n';
    CHECK(hal_uart_send(&kit.device.mIface, 0, &byte, 1) == ATCA_COMM_FAIL);
}

TEST_CASE("Tap pad hold: a tty that takes no writes ends within the bound", "[tappad_bounded]")
{
    const FakeModemLines modem;
    Pty pty;
    KitDevice kit(ATCA_UART_IFACE, pty.slave);
    REQUIRE(kit.open() == ATCA_SUCCESS);

    // Stop the tty's output, and fill what it buffers where the stop does not already refuse it.
    const int filler = ::open(pty.slave.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    REQUIRE(filler >= 0);
    REQUIRE(::tcflow(filler, TCOOFF) == 0);
    const std::vector<char> block(4096, 'x');
    while (::write(filler, block.data(), block.size()) > 0) { }
    REQUIRE(errno == EAGAIN);

    uint8_t revision[4] = {};
    const auto result = kit.info(revision, [&] { pty.closeMaster(); });

    // The pad saw none of the command: only the write bound ended the call.
    std::string seen;
    char buffer[4096];
    if (pty.master >= 0) { // still open unless the call had to be unblocked
        REQUIRE(::fcntl(pty.master, F_SETFL, O_NONBLOCK) == 0);
    }
    for (ssize_t n; pty.master >= 0 && (n = ::read(pty.master, buffer, sizeof(buffer))) > 0;) {
        seen.append(buffer, static_cast<size_t>(n));
    }
    ::tcflow(filler, TCOON);
    ::close(filler);

    CHECK(result.inBound);
    CHECK(result.status != ATCA_SUCCESS);
    CHECK(seen.find(':') == std::string::npos);
    CHECK(result.elapsed < std::chrono::milliseconds(HAL_UART_WRITE_TIMEOUT_MS + 1500));
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
    const auto result = kit.info(revision, [&] {
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
    FakeKitPad pad;
    const PtyPad player(pty, pad);
    KitDevice kit(ATCA_UART_IFACE, pty.slave);
    REQUIRE(kit.open() == ATCA_SUCCESS);

    uint8_t revision[4] = {};
    const auto result = kit.info(revision, [&] { pty.closeMaster(); });

    CHECK(result.status == ATCA_SUCCESS);
    CHECK(result.elapsed < 1s);
    CHECK(revision[3] == revisionOf(0));
    CHECK_FALSE(player.writeFailed());
    // The Info command, then the idle, byte for byte as the kit protocol frames them.
    CHECK(pad.commands() == INFO_COMMAND + "E:i()\n");
}

TEST_CASE("Tap pad hold: kit UART skips 64 NULs ahead of a reply and no more", "[tappad_bounded]")
{
    const FakeModemLines modem;
    Pty pty;
    KitDevice kit(ATCA_UART_IFACE, pty.slave);
    REQUIRE(kit.open() == ATCA_SUCCESS);

    const auto receive = [&](size_t nuls) {
        const std::string bytes = std::string(nuls, '\0') + "00()\n";
        REQUIRE(::write(pty.master, bytes.data(), bytes.size())
                == static_cast<ssize_t>(bytes.size()));
        char reply[16] = {};
        int size = sizeof(reply);
        const auto status =
                kit_phy_receive(&kit.device.mIface, reinterpret_cast<uint8_t *>(reply), &size);
        return status == ATCA_SUCCESS ? std::string(reply, static_cast<size_t>(size))
                                      : std::string("failed");
    };

    CHECK(receive(KIT_MAX_NUL_SKIP) == "00()");
    CHECK(receive(KIT_MAX_NUL_SKIP + 1) == "failed");
}

TEST_CASE("Tap pad hold: a tty pad slow to start its reply still answers", "[tappad_bounded]")
{
    const FakeModemLines modem;
    Pty pty;
    FakeKitPad pad([](int) { return Delay {1100ms}; });
    const PtyPad player(pty, pad);
    KitDevice kit(ATCA_UART_IFACE, pty.slave);
    REQUIRE(kit.open() == ATCA_SUCCESS);

    uint8_t revision[4] = {};
    const auto result = kit.info(revision, [&] { pty.closeMaster(); });

    CHECK(result.status == ATCA_SUCCESS);
    CHECK(revision[3] == revisionOf(0));
}

TEST_CASE("Tap pad hold: a tty reply that came too late is not read by the next command",
          "[tappad_bounded]")
{
    const FakeModemLines modem;
    Pty pty;
    FakeKitPad pad([](int n) { return n == 0 ? Delay {3500ms} : Delay {0ms}; });
    const PtyPad player(pty, pad);
    // Keeps the tty, and the input it queues, alive between the two openings.
    const int holder = ::open(pty.slave.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    REQUIRE(holder >= 0);

    uint8_t revision[4] = {};
    const auto start = Clock::now();
    {
        KitDevice first(ATCA_UART_IFACE, pty.slave);
        REQUIRE(first.open() == ATCA_SUCCESS);
        CHECK(first.info(revision, [&] { pty.closeMaster(); }).status != ATCA_SUCCESS);
    }
    std::this_thread::sleep_until(start + 4s); // the late reply is now queued on the tty

    KitDevice second(ATCA_UART_IFACE, pty.slave);
    REQUIRE(second.open() == ATCA_SUCCESS);
    const auto result = second.info(revision, [&] { pty.closeMaster(); });
    ::close(holder);

    CHECK(result.status == ATCA_SUCCESS);
    CHECK(revision[3] == revisionOf(1));
}

TEST_CASE("Tap pad hold: a HID pad that never answers ends within the bound", "[tappad_bounded]")
{
    FakeKitPad pad(FakeKitPad::Silent {});
    fake_hid::Device hid(pad);

    uint8_t revision[4] = {};
    const auto result = hid.kit.info(revision, [&] { pad.release(); });

    CHECK(result.inBound);
    CHECK(result.status != ATCA_SUCCESS);
    // One wait for the Info reply and one for the idle's; calib's other polls fail at once.
    CHECK(pad.timedReads() == 2);
    CHECK(lockFreeForOthers());
}

TEST_CASE("Tap pad hold: a healthy HID pad answers as before", "[tappad_bounded]")
{
    FakeKitPad pad;
    fake_hid::Device hid(pad);

    uint8_t revision[4] = {};
    const auto result = hid.kit.info(revision, [&] { pad.release(); });

    CHECK(result.status == ATCA_SUCCESS);
    CHECK(result.elapsed < 1s);
    CHECK(revision[3] == revisionOf(0));
    CHECK(pad.commands() == INFO_COMMAND + "E:i()\n");
}

TEST_CASE("Tap pad hold: a HID pad slow to start its reply still answers", "[tappad_bounded]")
{
    FakeKitPad pad([](int) { return Delay {1100ms}; });
    fake_hid::Device hid(pad);

    uint8_t revision[4] = {};
    const auto result = hid.kit.info(revision, [&] { pad.release(); });

    CHECK(result.status == ATCA_SUCCESS);
    CHECK(revision[3] == revisionOf(0));
}

#    if defined(ATCA_HAL_KIT_UART_LIBUSB)

namespace fake_cdc {

FakeKitPad *pad = nullptr;
std::atomic<int> opens {0};

ATCA_STATUS open(const char *, uint32_t, atca_cdc_libusb_t **cdc)
{
    ++opens;
    *cdc = hal_cdc_libusb_test_new();
    return *cdc != nullptr ? ATCA_SUCCESS : ATCA_ALLOC_FAILURE;
}

constexpr int LIBUSB_TIMEOUT = -7; // LIBUSB_ERROR_TIMEOUT

int transfer(unsigned char endpoint, unsigned char *data, int length, int *transferred,
             unsigned int timeoutMs)
{
    if ((endpoint & 0x80u) == 0) {
        pad->write(reinterpret_cast<const char *>(data), static_cast<size_t>(length));
        *transferred = length;
        return 0;
    }
    *transferred =
            static_cast<int>(pad->read(reinterpret_cast<char *>(data), static_cast<size_t>(length),
                                       static_cast<int>(timeoutMs)));
    return *transferred > 0 ? 0 : LIBUSB_TIMEOUT;
}

// A libusb CDC kit device whose transfers go to and from padIn.
struct Device {
    explicit Device(FakeKitPad &padIn)
    {
        pad = &padIn;
        opens = 0;
        hal_uart_test_set_cdc_open(open);
        hal_cdc_libusb_test_set_transfer(transfer);
        REQUIRE(kit.open() == ATCA_SUCCESS);
    }
    ~Device()
    {
        hal_uart_test_set_cdc_open(nullptr);
        hal_cdc_libusb_test_set_transfer(nullptr);
        pad = nullptr;
    }
    Device(const Device &) = delete;
    Device &operator=(const Device &) = delete;

    KitDevice kit {ATCA_UART_IFACE, "usb:001:002:1"};
};

} // namespace fake_cdc

TEST_CASE("Tap pad hold: a libusb CDC pad that never answers ends within the bound",
          "[tappad_bounded]")
{
    FakeKitPad pad(FakeKitPad::Silent {});
    fake_cdc::Device cdc(pad);

    uint8_t revision[4] = {};
    const auto result = cdc.kit.info(revision, [&] { pad.release(); });

    CHECK(result.inBound);
    CHECK(result.status != ATCA_SUCCESS);
    // Opened by init and reopened by the idle's send, never by calib's polls; one wait for the
    // Info reply and one for the idle's.
    CHECK(fake_cdc::opens == 2);
    CHECK(pad.timedReads() == 2);
    CHECK(lockFreeForOthers());
}

TEST_CASE("Tap pad hold: a libusb CDC pad slow to start its reply still answers",
          "[tappad_bounded]")
{
    FakeKitPad pad([](int) { return Delay {1100ms}; });
    fake_cdc::Device cdc(pad);

    uint8_t revision[4] = {};
    const auto result = cdc.kit.info(revision, [&] { pad.release(); });

    CHECK(result.status == ATCA_SUCCESS);
    CHECK(revision[3] == revisionOf(0));
    CHECK(fake_cdc::opens == 1);
}

TEST_CASE("Tap pad hold: a libusb CDC reply that came too late is not read by the next command",
          "[tappad_bounded]")
{
    FakeKitPad pad([](int n) { return n == 0 ? Delay {3500ms} : Delay {0ms}; });
    fake_cdc::Device cdc(pad);

    uint8_t revision[4] = {};
    const auto start = Clock::now();
    CHECK(cdc.kit.info(revision, [&] { pad.release(); }).status != ATCA_SUCCESS);
    std::this_thread::sleep_until(start + 4s); // the late reply is now queued in the pad

    const auto result = cdc.kit.info(revision, [&] { pad.release(); });

    CHECK(result.status == ATCA_SUCCESS);
    CHECK(revision[3] == revisionOf(1));
}

#    endif

#endif
