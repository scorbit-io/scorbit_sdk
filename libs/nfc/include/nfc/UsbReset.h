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

// The probe's bootloader command, after which it resets, written straight to its CDC data
// interface over usbfs with the serial port closed and nothing left to read. On some USB hosts a
// cdc-acm port still open across that reset takes the host controller down with it. A "usb:"
// probe (libusb, no cdc-acm) goes this way too, so no reply-wait bulk IN is pending at the reset.

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nfc::usb {

/** The bootloader command's argument. */
constexpr std::array<uint8_t, 4> BOOTLOADER_PAYLOAD {0x05, 0xC0, 0x4B, 0x17};

/** The bootloader command frame: header, command, address 0, length, payload, checksum. */
std::vector<uint8_t> bootloaderFrame();

/** Where a probe's data interface sits; its comm interface is the one before it. */
struct ResetTarget {
    unsigned bus {0};
    unsigned address {0};
    int dataInterface {-1};

    bool operator==(const ResetTarget &) const = default;
};

/**
 * The reset target of the probe opened on @p device: for "/dev/ttyACM<n>", the data interface
 * after the comm interface its tty is bound to (sysfs); for "usb:<bus>:<addr>:<iface>", iface.
 */
std::optional<ResetTarget> resetTargetOf(const std::string &device,
                                         const std::filesystem::path &sys = "/sys");

/** A probe's usbfs device, opened while its serial port is still open. */
class UsbfsReset
{
public:
    /**
     * Opens the device and finds its data interface's bulk OUT endpoint, touching no interface, so
     * a missing permission shows before the serial port is closed. Null off Linux or on failure.
     */
    static std::unique_ptr<UsbfsReset> open(const ResetTarget &target);
    ~UsbfsReset();
    UsbfsReset(const UsbfsReset &) = delete;
    UsbfsReset &operator=(const UsbfsReset &) = delete;

    /**
     * Detaches any kernel driver from the comm and data interfaces, claims the data interface and
     * writes @p frame as one bulk OUT, with no read. On failure the drivers are re-attached, so the
     * probe's serial port comes back.
     */
    bool send(const std::vector<uint8_t> &frame);

private:
    UsbfsReset(void *handle, ResetTarget target, uint8_t endpointOut);

    void *m_handle;
    ResetTarget m_target;
    uint8_t m_endpointOut;
};

enum class ResetRoute {
    Usb,       ///< sent over usbfs with the serial port closed
    UsbFailed, ///< the serial port was closed, but the usbfs send failed
    SerialPort ///< no usbfs: sent over the serial port as before
};

/**
 * With @p sendOverUsb, closes the serial port first and then sends over usbfs, never falling back
 * to the closed port; without it, calls @p sendOverPort.
 */
ResetRoute sendReset(const std::function<bool()> &sendOverUsb,
                     const std::function<void()> &closePort,
                     const std::function<void()> &sendOverPort);

} // namespace nfc::usb
