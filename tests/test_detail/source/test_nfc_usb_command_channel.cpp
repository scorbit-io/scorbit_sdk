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

// The bootloader command sent over usbfs with the probe's serial port closed: its frame, where its
// channel is found, and the order of close and send. The usbfs send itself needs a probe.

#include <nfc/UsbCommandChannel.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nfc::usb::CommandTarget;
using nfc::usb::ResetRoute;

namespace {

struct FakeSysfs {
    FakeSysfs()
    {
        std::random_device rd;
        root = fs::temp_directory_path() / ("scorbit-usb-cmd-" + std::to_string(rd()));
        fs::create_directories(root / "bus/usb/devices");
        fs::create_directories(root / "class/tty");
    }
    ~FakeSysfs() { fs::remove_all(root); }

    // A probe at @p port whose tty @p tty is bound to comm interface @p comm.
    void probe(const std::string &port, unsigned bus, unsigned dev, const std::string &tty,
               const std::string &comm)
    {
        const auto dir = root / "bus/usb/devices" / port;
        const auto iface = dir / (port + ":1." + comm);
        fs::create_directories(iface);
        std::ofstream(dir / "busnum") << bus << "\n";
        std::ofstream(dir / "devnum") << dev << "\n";
        std::ofstream(iface / "bInterfaceNumber") << "0" << comm << "\n";
        fs::create_directories(root / "class/tty" / tty);
        fs::create_directory_symlink(iface, root / "class/tty" / tty / "device");
    }

    fs::path root;
};

// sendResetCommand owns and destroys the channel, so what it saw is kept by the test.
struct FakeChannel : nfc::usb::CommandChannel {
    FakeChannel(std::vector<std::string> &events, std::vector<uint8_t> &sent, bool ok)
        : events {events}
        , sent {sent}
        , ok {ok}
    {
    }
    bool send(const std::vector<uint8_t> &frame) override
    {
        sent = frame;
        events.push_back("send");
        return ok;
    }
    std::vector<std::string> &events;
    std::vector<uint8_t> &sent;
    bool ok;
};

} // namespace

TEST_CASE("The bootloader frame is the one proven on the lab board", "[nfc][usb]")
{
    const std::vector<uint8_t> expected {0x03, 0x45, 0xf7, 0x00, 0x00, 0x04, 0x00,
                                         0x05, 0xc0, 0x4b, 0x17, 0x95, 0xfd};
    CHECK(nfc::usb::commandFrame(0xf7, 0, {0x05, 0xc0, 0x4b, 0x17}) == expected);
}

TEST_CASE("A frame carries its address and length little endian", "[nfc][usb]")
{
    const auto frame = nfc::usb::commandFrame(0x10, 0x1234, std::vector<uint8_t>(300, 0));
    REQUIRE(frame.size() == 7 + 300 + 2);
    CHECK(frame[3] == 0x34);
    CHECK(frame[4] == 0x12);
    CHECK(frame[5] == 0x2c);
    CHECK(frame[6] == 0x01);
}

TEST_CASE("A libusb probe path names its data interface", "[nfc][usb]")
{
    CHECK(nfc::usb::commandTargetOf("usb:001:007:1") == CommandTarget {1, 7, 0, 1});
    CHECK(nfc::usb::commandTargetOf("usb:2:9:3") == CommandTarget {2, 9, 2, 3});
    CHECK_FALSE(nfc::usb::commandTargetOf("COM3"));
}

#if !defined(_WIN32) // creating the symlink needs privileges on Windows
TEST_CASE("A ttyACM probe maps to its comm interface and the data interface after it", "[nfc][usb]")
{
    FakeSysfs sys;
    sys.probe("1-1", 1, 30, "ttyACM0", "0");
    sys.probe("1-2", 1, 31, "ttyACM2", "2");

    CHECK(nfc::usb::commandTargetOf("/dev/ttyACM0", sys.root) == CommandTarget {1, 30, 0, 1});
    CHECK(nfc::usb::commandTargetOf("/dev/ttyACM2", sys.root) == CommandTarget {1, 31, 2, 3});
    CHECK_FALSE(nfc::usb::commandTargetOf("/dev/ttyACM5", sys.root));
}
#endif

TEST_CASE("The serial port is closed before the reset command goes over usbfs", "[nfc][usb]")
{
    std::vector<std::string> events;
    std::vector<uint8_t> sent;
    const std::vector<uint8_t> frame {1, 2, 3};
    const bool ok = GENERATE(true, false);

    const auto route = nfc::usb::sendResetCommand(
            std::make_unique<FakeChannel>(events, sent, ok), frame,
            [&] { events.push_back("close"); }, [&] { events.push_back("port"); });

    CHECK(route == (ok ? ResetRoute::Usb : ResetRoute::UsbFailed));
    // Never over the port once it is closed, even when the usbfs send fails.
    CHECK(events == std::vector<std::string> {"close", "send"});
    CHECK(sent == frame);
}

TEST_CASE("Without a usbfs channel the reset command goes over the serial port", "[nfc][usb]")
{
    std::vector<std::string> events;
    const auto route = nfc::usb::sendResetCommand(
            nullptr, {1, 2, 3}, [&] { events.push_back("close"); },
            [&] { events.push_back("port"); });

    CHECK(route == ResetRoute::SerialPort);
    CHECK(events == std::vector<std::string> {"port"});
}
