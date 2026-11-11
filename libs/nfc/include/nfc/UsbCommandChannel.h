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

// A probe command written straight to the probe's CDC data interface over usbfs, with no serial
// port open and no transfer left pending. It is for the one command after which the probe resets
// (bootloader): on some USB hosts a cdc-acm port still open across that reset takes the host
// controller down with it.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nfc::usb {

/** Where a probe's command channel sits on the bus. */
struct CommandTarget {
    unsigned bus {0};
    unsigned address {0};
    int commInterface {-1}; ///< -1 when unknown
    int dataInterface {-1};

    bool operator==(const CommandTarget &other) const
    {
        return bus == other.bus && address == other.address && commInterface == other.commInterface
            && dataInterface == other.dataInterface;
    }
};

/**
 * The command channel of the probe opened on @p device. For "/dev/ttyACM<n>" it is read from
 * sysfs: the comm interface the tty is bound to, and the data interface after it. For
 * "usb:<bus>:<addr>:<iface>", iface is the data interface. Empty wherever sysfs is absent.
 */
std::optional<CommandTarget> commandTargetOf(const std::string &device,
                                             const std::filesystem::path &sys = "/sys");

/** A probe command frame: header, command, address, length, data, then the checksum. */
std::vector<uint8_t> commandFrame(uint8_t cmd, uint16_t address, const std::vector<uint8_t> &data);

class CommandChannel
{
public:
    virtual ~CommandChannel() = default;
    /**
     * Detaches any kernel driver from the comm and data interfaces, claims the data interface and
     * writes @p frame as one bulk OUT, with no read. On failure the drivers are re-attached, so the
     * probe's serial port comes back.
     */
    virtual bool send(const std::vector<uint8_t> &frame) = 0;
};

/**
 * Opens the probe's usbfs device and checks its data interface, touching no interface yet, so
 * that a missing permission shows while the serial port is still open. Null off Linux, or when
 * the device cannot be found or opened.
 */
std::unique_ptr<CommandChannel> openCommandChannel(const CommandTarget &target);

enum class ResetRoute {
    Usb,       ///< sent over usbfs with the serial port closed
    UsbFailed, ///< the serial port was closed, but the usbfs send failed
    SerialPort ///< no usbfs channel: sent over the serial port as before
};

/**
 * Sends a command after which the probe resets. With a @p usb channel, closes the serial port
 * first and sends @p frame over usbfs; without one, calls @p sendOverPort.
 */
ResetRoute sendResetCommand(std::unique_ptr<CommandChannel> usb, const std::vector<uint8_t> &frame,
                            const std::function<void()> &closePort,
                            const std::function<void()> &sendOverPort);

} // namespace nfc::usb
