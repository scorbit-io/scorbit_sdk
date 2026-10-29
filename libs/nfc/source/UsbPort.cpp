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

#include "nfc/UsbPort.h"

#include <cstdio>
#include <fstream>
#include <system_error>

namespace nfc::usb {

namespace fs = std::filesystem;

std::optional<unsigned> readSysfsNumber(const fs::path &file, int base)
{
    std::ifstream in(file);
    std::string text;
    if (!(in >> text)) {
        return std::nullopt;
    }
    try {
        size_t used = 0;
        const unsigned long value = std::stoul(text, &used, base);
        if (used != text.size()) {
            return std::nullopt;
        }
        return unsigned(value);
    } catch (const std::exception &) {
        return std::nullopt;
    }
}

std::optional<std::string> portOfDevice(const std::string &device, const fs::path &sys)
{
    std::error_code ec;
    unsigned bus = 0, address = 0, iface = 0;
    if (std::sscanf(device.c_str(), "usb:%u:%u:%u", &bus, &address, &iface) == 3) {
        for (fs::directory_iterator it(sys / "bus/usb/devices", ec), end; !ec && it != end;
             it.increment(ec)) {
            const auto name = it->path().filename().string();
            if (name.find(':') == std::string::npos
                && readSysfsNumber(it->path() / "busnum", 10) == bus
                && readSysfsNumber(it->path() / "devnum", 10) == address) {
                return name;
            }
        }
        return std::nullopt;
    }

    // A tty's "device" link points at its USB interface, e.g. ".../usb1/1-1/1-1:1.0".
    const auto tty = fs::path(device).filename();
    if (tty.empty() || device.rfind("/dev/", 0) != 0) {
        return std::nullopt;
    }
    const auto interface = fs::canonical(sys / "class/tty" / tty / "device", ec);
    if (ec) {
        return std::nullopt;
    }
    const auto name = interface.filename().string();
    const auto colon = name.find(':');
    if (colon == std::string::npos || colon == 0) {
        return std::nullopt;
    }
    return name.substr(0, colon);
}

std::optional<PortDevice> deviceAtPort(const std::string &port, const fs::path &sys)
{
    if (port.empty() || port.find_first_of(":/") != std::string::npos) {
        return std::nullopt;
    }
    const auto dir = sys / "bus/usb/devices" / port;
    const auto vendor = readSysfsNumber(dir / "idVendor", 16);
    const auto product = readSysfsNumber(dir / "idProduct", 16);
    const auto bus = readSysfsNumber(dir / "busnum", 10);
    const auto address = readSysfsNumber(dir / "devnum", 10);
    if (!vendor || !product || !bus || !address) {
        return std::nullopt;
    }
    return PortDevice {uint16_t(*vendor), uint16_t(*product), *bus, *address};
}

std::string deviceNode(const PortDevice &device)
{
    char node[32];
    std::snprintf(node, sizeof(node), "/dev/bus/usb/%03u/%03u", device.bus, device.address);
    return node;
}

} // namespace nfc::usb
