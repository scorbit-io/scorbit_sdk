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

// SB-5759: a tap pad's USB port path, from a fake sysfs tree.

#include <nfc/UsbPort.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;

namespace {

struct FakeSysfs {
    FakeSysfs()
    {
        std::random_device rd;
        root = fs::temp_directory_path() / ("scorbit-usb-port-" + std::to_string(rd()));
        fs::create_directories(root / "bus/usb/devices");
        fs::create_directories(root / "class/tty");
    }
    ~FakeSysfs() { fs::remove_all(root); }

    fs::path device(const std::string &port, const std::string &vid, const std::string &pid,
                    unsigned bus, unsigned dev)
    {
        const auto dir = root / "bus/usb/devices" / port;
        fs::create_directories(dir);
        std::ofstream(dir / "idVendor") << vid << "\n";
        std::ofstream(dir / "idProduct") << pid << "\n";
        std::ofstream(dir / "busnum") << bus << "\n";
        std::ofstream(dir / "devnum") << dev << "\n";
        return dir;
    }

    fs::path root;
};

} // namespace

TEST_CASE("usb port: a libusb probe path maps to its port", "[nfc][usb]")
{
    FakeSysfs sys;
    sys.device("usb1", "1d6b", "0002", 1, 1);
    sys.device("1-1", "cafe", "4002", 1, 7);
    sys.device("1-2", "cafe", "4002", 1, 9);

    CHECK(nfc::usb::portOfDevice("usb:001:007:1", sys.root) == "1-1");
    CHECK(nfc::usb::portOfDevice("usb:1:9:3", sys.root) == "1-2");
    CHECK_FALSE(nfc::usb::portOfDevice("usb:001:042:1", sys.root));
    CHECK_FALSE(nfc::usb::portOfDevice("COM3", sys.root));
}

#if !defined(_WIN32) // creating the symlink needs privileges on Windows
TEST_CASE("usb port: a ttyACM probe maps to the port of its interface", "[nfc][usb]")
{
    FakeSysfs sys;
    const auto dir = sys.device("1-1.3", "cafe", "4002", 1, 12);
    fs::create_directories(dir / "1-1.3:1.0");
    fs::create_directories(sys.root / "class/tty/ttyACM0");
    fs::create_directory_symlink(dir / "1-1.3:1.0", sys.root / "class/tty/ttyACM0/device");

    CHECK(nfc::usb::portOfDevice("/dev/ttyACM0", sys.root) == "1-1.3");
    CHECK_FALSE(nfc::usb::portOfDevice("/dev/ttyACM1", sys.root));
    CHECK_FALSE(nfc::usb::portOfDevice("ttyACM0", sys.root));
}
#endif

TEST_CASE("usb port: what is at a port, and its usbfs node", "[nfc][usb]")
{
    FakeSysfs sys;
    sys.device("1-1", "2e8a", "0003", 1, 8);

    const auto at = nfc::usb::deviceAtPort("1-1", sys.root);
    REQUIRE(at);
    CHECK(at->vendorId == 0x2e8a);
    CHECK(at->productId == 0x0003);
    CHECK(nfc::usb::deviceNode(*at) == "/dev/bus/usb/001/008");

    CHECK_FALSE(nfc::usb::deviceAtPort("1-2", sys.root));
    CHECK_FALSE(nfc::usb::deviceAtPort("../1-1", sys.root));
    CHECK_FALSE(nfc::usb::deviceAtPort("1-1:1.0", sys.root));
    CHECK_FALSE(nfc::usb::deviceAtPort("", sys.root));
}
