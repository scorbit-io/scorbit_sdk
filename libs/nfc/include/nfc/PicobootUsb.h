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

#include "nfc/Picoboot.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nfc::picoboot {

/** A pad in BOOTSEL (2e8a:0003) on the bus. */
struct BootselDevice {
    /** sysfs port path, e.g. "1-1" or "1-1.3". A pad keeps it across the switch to BOOTSEL. */
    std::string port;
    unsigned bus {0};
    unsigned address {0};
};

/**
 * Every 2e8a:0003 device, found by vendor and product id rather than by an RPI-RP2 volume.
 *
 * Read from sysfs rather than libusb_get_port_numbers, which the libusb on older hosts lacks.
 * Empty where sysfs does not exist (anything but Linux).
 */
std::vector<BootselDevice>
listBootselDevices(const std::filesystem::path &sysfsDevices = "/sys/bus/usb/devices");

#if defined(__linux__)

enum class OpenError {
    None,
    NotFound,
    NoInterface,
    /** The device node is not writable by this process (no udev rule, or not in its group). */
    AccessDenied,
    Busy,
    Failed,
};

const char *openErrorName(OpenError error);

/** The PICOBOOT interface of one BOOTSEL device, claimed through libusb. */
class LibusbTransport : public Transport
{
public:
    static std::unique_ptr<LibusbTransport> open(const BootselDevice &device, OpenError &error);

    ~LibusbTransport() override;
    LibusbTransport(const LibusbTransport &) = delete;
    LibusbTransport &operator=(const LibusbTransport &) = delete;

    UsbStatus resetInterface() override;
    UsbStatus bulkOut(const uint8_t *data, size_t size, unsigned timeoutMs) override;
    UsbStatus bulkIn(uint8_t *data, size_t size, size_t &received, unsigned timeoutMs) override;

private:
    LibusbTransport(void *handle, InterfaceSelection selection);

    void *m_handle; // libusb_device_handle, kept opaque so this header needs no libusb.h
    InterfaceSelection m_interface;
};

#endif

} // namespace nfc::picoboot
