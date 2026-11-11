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

#include "nfc/UsbCommandChannel.h"
#include "nfc/UsbPort.h"

#include <cstdio>
#include <system_error>

#if defined(__linux__)
// ListUsbDevices.h brings libusb, the shared libusb context and the probe's USB ids.
#    include "nfc/Util.h"
#    include "nfc/ListUsbDevices.h"
#endif

namespace fs = std::filesystem;

namespace nfc::usb {

std::optional<CommandTarget> commandTargetOf(const std::string &device, const fs::path &sys)
{
    unsigned bus = 0, address = 0, iface = 0;
    if (std::sscanf(device.c_str(), "usb:%u:%u:%u", &bus, &address, &iface) == 3) {
        return CommandTarget {bus, address, iface > 0 ? int(iface) - 1 : -1, int(iface)};
    }

    // A tty's "device" link points at its comm interface, e.g. ".../usb1/1-1/1-1:1.0".
    const auto tty = fs::path(device).filename();
    if (tty.empty() || device.rfind("/dev/", 0) != 0) {
        return std::nullopt;
    }
    std::error_code ec;
    const auto interface = fs::canonical(sys / "class/tty" / tty / "device", ec);
    if (ec) {
        return std::nullopt;
    }
    const auto number = readSysfsNumber(interface / "bInterfaceNumber", 16);
    const auto deviceDir = interface.parent_path();
    const auto busnum = readSysfsNumber(deviceDir / "busnum", 10);
    const auto devnum = readSysfsNumber(deviceDir / "devnum", 10);
    if (!number || !busnum || !devnum) {
        return std::nullopt;
    }
    return CommandTarget {*busnum, *devnum, int(*number), int(*number) + 1};
}

std::vector<uint8_t> commandFrame(uint8_t cmd, uint16_t address, const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> frame {0x03,
                                0x45,
                                cmd,
                                uint8_t(address),
                                uint8_t(address >> 8),
                                uint8_t(data.size()),
                                uint8_t(data.size() >> 8)};
    frame.insert(frame.end(), data.begin(), data.end());
    // Same checksum as SerialCable::AddCrc: one's complement of the byte sum, little endian.
    uint16_t sum = 0;
    for (const auto byte : frame) {
        sum = uint16_t(sum + byte);
    }
    const uint16_t crc = uint16_t(~sum);
    frame.push_back(uint8_t(crc));
    frame.push_back(uint8_t(crc >> 8));
    return frame;
}

ResetRoute sendResetCommand(std::unique_ptr<CommandChannel> usb, const std::vector<uint8_t> &frame,
                            const std::function<void()> &closePort,
                            const std::function<void()> &sendOverPort)
{
    if (!usb) {
        sendOverPort();
        return ResetRoute::SerialPort;
    }
    closePort();
    return usb->send(frame) ? ResetRoute::Usb : ResetRoute::UsbFailed;
}

#if defined(__linux__)

namespace {

constexpr unsigned SEND_TIMEOUT_MS = 1000;

class UsbfsCommandChannel final : public CommandChannel
{
public:
    UsbfsCommandChannel(libusb_device_handle *handle, CommandTarget target,
                        unsigned char endpointOut)
        : m_handle {handle}
        , m_target {target}
        , m_endpointOut {endpointOut}
    {
    }

    ~UsbfsCommandChannel() override { libusb_close(m_handle); }

    UsbfsCommandChannel(const UsbfsCommandChannel &) = delete;
    UsbfsCommandChannel &operator=(const UsbfsCommandChannel &) = delete;

    bool send(const std::vector<uint8_t> &frame) override
    {
        std::vector<int> detached;
        for (const int iface : {m_target.commInterface, m_target.dataInterface}) {
            if (iface < 0 || libusb_kernel_driver_active(m_handle, iface) != 1) {
                continue;
            }
            if (const int rc = libusb_detach_kernel_driver(m_handle, iface); rc != 0) {
                ERR("NFC: can't detach the driver of interface %d (%s)\n", iface,
                    libusb_error_name(rc));
                reattach(detached);
                return false;
            }
            detached.push_back(iface);
        }
        if (const int rc = libusb_claim_interface(m_handle, m_target.dataInterface); rc != 0) {
            ERR("NFC: can't claim interface %d (%s)\n", m_target.dataInterface,
                libusb_error_name(rc));
            reattach(detached);
            return false;
        }

        // libusb takes a non-const buffer even for OUT.
        auto buffer = frame;
        int transferred = 0;
        const int rc = libusb_bulk_transfer(m_handle, m_endpointOut, buffer.data(),
                                            int(buffer.size()), &transferred, SEND_TIMEOUT_MS);
        libusb_release_interface(m_handle, m_target.dataInterface);
        if (rc != 0 || size_t(transferred) != buffer.size()) {
            ERR("NFC: usbfs send failed (%s, %d of %zu bytes)\n", libusb_error_name(rc),
                transferred, buffer.size());
            reattach(detached); // the probe stays: give its serial port back
            return false;
        }
        // Not re-attached on success: the probe is resetting, and its new firmware enumerates
        // afresh.
        return true;
    }

private:
    void reattach(const std::vector<int> &interfaces)
    {
        for (const int iface : interfaces) {
            libusb_attach_kernel_driver(m_handle, iface);
        }
    }

    libusb_device_handle *m_handle;
    CommandTarget m_target;
    unsigned char m_endpointOut;
};

// The bulk OUT endpoint of the data interface, or 0.
unsigned char dataEndpointOut(libusb_device *device, int dataInterface)
{
    libusb_config_descriptor *cfg = nullptr;
    if (libusb_get_active_config_descriptor(device, &cfg) != 0) {
        return 0;
    }
    unsigned char endpoint = 0;
    for (int i = 0; i < cfg->bNumInterfaces && !endpoint; ++i) {
        if (cfg->interface[i].num_altsetting < 1) {
            continue;
        }
        const auto &alt = cfg->interface[i].altsetting[0];
        if (alt.bInterfaceNumber != dataInterface || alt.bInterfaceClass != LIBUSB_CLASS_DATA) {
            continue;
        }
        for (int e = 0; e < alt.bNumEndpoints; ++e) {
            const auto &ep = alt.endpoint[e];
            if ((ep.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK
                && !(ep.bEndpointAddress & LIBUSB_ENDPOINT_IN)) {
                endpoint = ep.bEndpointAddress;
                break;
            }
        }
    }
    libusb_free_config_descriptor(cfg);
    return endpoint;
}

} // namespace

std::unique_ptr<CommandChannel> openCommandChannel(const CommandTarget &target)
{
    if (target.dataInterface < 0) {
        return nullptr;
    }
    libusb_context *ctx = spb::sdk::probeLibusbContext();
    if (!ctx) {
        return nullptr;
    }
    libusb_device **list = nullptr;
    const ssize_t count = libusb_get_device_list(ctx, &list);
    if (count < 0) {
        return nullptr;
    }

    libusb_device *found = nullptr;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device_descriptor desc {};
        if (libusb_get_bus_number(list[i]) == target.bus
            && libusb_get_device_address(list[i]) == target.address
            && libusb_get_device_descriptor(list[i], &desc) == 0
            && desc.idVendor == spb::sdk::kProbeUsbVid && spb::sdk::isProbeUsbPid(desc.idProduct)) {
            found = list[i];
            break;
        }
    }
    const unsigned char endpointOut = found ? dataEndpointOut(found, target.dataInterface) : 0;
    libusb_device_handle *handle = nullptr;
    const int rc = endpointOut ? libusb_open(found, &handle) : LIBUSB_ERROR_NOT_FOUND;
    libusb_free_device_list(list, 1);
    if (rc != 0) {
        ERR("NFC: can't open the probe over usbfs (%s)\n", libusb_error_name(rc));
        return nullptr;
    }
    return std::make_unique<UsbfsCommandChannel>(handle, target, endpointOut);
}

#else

std::unique_ptr<CommandChannel> openCommandChannel(const CommandTarget &)
{
    return nullptr;
}

#endif

} // namespace nfc::usb
