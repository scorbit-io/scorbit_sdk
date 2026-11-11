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

// The bootloader command sent over usbfs with the probe's serial port closed: its frame and the
// order of close and send. Finding a ttyACM probe's target is in test_nfc_usb_port.cpp; the usbfs
// send itself needs a probe.

#include <nfc/UsbReset.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>
#include <vector>

using nfc::usb::ResetRoute;
using nfc::usb::ResetTarget;

TEST_CASE("The bootloader frame is the one proven on the lab board", "[nfc][usb]")
{
    const std::vector<uint8_t> expected {0x03, 0x45, 0xf7, 0x00, 0x00, 0x04, 0x00,
                                         0x05, 0xc0, 0x4b, 0x17, 0x95, 0xfd};
    CHECK(nfc::usb::bootloaderFrame() == expected);
}

TEST_CASE("A libusb probe path names its data interface", "[nfc][usb]")
{
    CHECK(nfc::usb::resetTargetOf("usb:001:007:1") == ResetTarget {1, 7, 1});
    CHECK(nfc::usb::resetTargetOf("usb:2:9:3") == ResetTarget {2, 9, 3});
    CHECK_FALSE(nfc::usb::resetTargetOf("COM3"));
}

TEST_CASE("The serial port is closed before the reset goes over usbfs", "[nfc][usb]")
{
    std::vector<std::string> events;
    const bool ok = GENERATE(true, false);

    const auto route = nfc::usb::sendReset(
            [&] {
                events.push_back("usb");
                return ok;
            },
            [&] { events.push_back("close"); }, [&] { events.push_back("port"); });

    CHECK(route == (ok ? ResetRoute::Usb : ResetRoute::UsbFailed));
    // Never over the port once it is closed, even when the usbfs send fails.
    CHECK(events == std::vector<std::string> {"close", "usb"});
}

TEST_CASE("Without usbfs the reset goes over the serial port", "[nfc][usb]")
{
    std::vector<std::string> events;
    const auto route = nfc::usb::sendReset(
            {}, [&] { events.push_back("close"); }, [&] { events.push_back("port"); });

    CHECK(route == ResetRoute::SerialPort);
    CHECK(events == std::vector<std::string> {"port"});
}
