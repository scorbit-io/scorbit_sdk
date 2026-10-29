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

#pragma once

// USB port paths from sysfs. A device keeps its port path ("1-1", "1-1.3") when it re-enumerates,
// as a tap pad does on its way into and out of BOOTSEL, while its bus address changes. Empty
// results wherever sysfs is absent (anything but Linux).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace nfc::usb {

/** A sysfs attribute holding one number, e.g. idVendor (base 16) or busnum (base 10). */
std::optional<unsigned> readSysfsNumber(const std::filesystem::path &file, int base);

/**
 * Port path of the USB device a probe was opened on: @p device is "/dev/ttyACM<n>" or
 * "usb:<bus>:<addr>:<iface>".
 */
std::optional<std::string> portOfDevice(const std::string &device,
                                        const std::filesystem::path &sys = "/sys");

struct PortDevice {
    uint16_t vendorId {0};
    uint16_t productId {0};
    unsigned bus {0};
    unsigned address {0};
};

/** The device now at @p port, if any. */
std::optional<PortDevice> deviceAtPort(const std::string &port,
                                       const std::filesystem::path &sys = "/sys");

/** usbfs node of a device, "/dev/bus/usb/<bus>/<addr>": what libusb opens. */
std::string deviceNode(const PortDevice &device);

} // namespace nfc::usb
