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

#include "nfc/UsbReset.h"
#include "nfc/UsbPort.h"

#if defined(__linux__)
// ListUsbDevices.h brings libusb, the shared libusb context and the probe's USB ids.
#    include "nfc/Util.h"
#    include "nfc/ListUsbDevices.h"
#endif

namespace fs = std::filesystem;

namespace nfc::usb {

namespace {

constexpr uint8_t BOOTLOADER_COMMAND = 0xF7;

} // namespace

std::vector<uint8_t> bootloaderFrame()
{
    std::vector<uint8_t> frame {
            0x03, 0x45, BOOTLOADER_COMMAND, 0, 0, uint8_t(BOOTLOADER_PAYLOAD.size()), 0};
    frame.insert(frame.end(), BOOTLOADER_PAYLOAD.begin(), BOOTLOADER_PAYLOAD.end());
    // Same checksum as SerialCable::AddCrc: one's complement of the byte sum, little endian.
    uint16_t sum = 0;
    for (const auto byte : frame) {
        sum = uint16_t(sum + byte);
    }
    frame.push_back(uint8_t(~sum));
    frame.push_back(uint8_t(uint16_t(~sum) >> 8));
    return frame;
}

std::optional<ResetTarget> resetTargetOf(const std::string &device, const fs::path &sys)
{
    if (const auto usb = parseUsbPath(device)) {
        return ResetTarget {usb->bus, usb->address, int(usb->iface)};
    }
    const auto interface = ttyInterfaceDir(device, sys);
    if (!interface) {
        return std::nullopt;
    }
    const auto comm = readSysfsNumber(*interface / "bInterfaceNumber", 16);
    const auto bus = readSysfsNumber(interface->parent_path() / "busnum", 10);
    const auto address = readSysfsNumber(interface->parent_path() / "devnum", 10);
    if (!comm || !bus || !address) {
        return std::nullopt;
    }
    return ResetTarget {*bus, *address, int(*comm) + 1};
}

ResetRoute sendReset(const std::function<bool()> &sendOverUsb,
                     const std::function<void()> &closePort,
                     const std::function<void()> &sendOverPort)
{
    if (!sendOverUsb) {
        sendOverPort();
        return ResetRoute::SerialPort;
    }
    closePort();
    return sendOverUsb() ? ResetRoute::Usb : ResetRoute::UsbFailed;
}

#if defined(__linux__)

namespace {

constexpr unsigned SEND_TIMEOUT_MS = 1000;

libusb_device_handle *handleOf(void *handle)
{
    return static_cast<libusb_device_handle *>(handle);
}

// The bulk OUT endpoint of the data interface, or 0.
uint8_t dataEndpointOut(libusb_device *device, int dataInterface)
{
    libusb_config_descriptor *cfg = nullptr;
    if (libusb_get_active_config_descriptor(device, &cfg) != 0) {
        return 0;
    }
    uint8_t endpoint = 0;
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

std::unique_ptr<UsbfsReset> UsbfsReset::open(const ResetTarget &target)
{
    libusb_context *ctx = target.dataInterface >= 0 ? spb::sdk::probeLibusbContext() : nullptr;
    libusb_device **list = nullptr;
    if (!ctx || libusb_get_device_list(ctx, &list) < 0) {
        return nullptr;
    }
    libusb_device *found = nullptr;
    for (auto **it = list; *it && !found; ++it) {
        libusb_device_descriptor desc {};
        if (libusb_get_bus_number(*it) == target.bus
            && libusb_get_device_address(*it) == target.address
            && libusb_get_device_descriptor(*it, &desc) == 0
            && desc.idVendor == spb::sdk::kProbeUsbVid && spb::sdk::isProbeUsbPid(desc.idProduct)) {
            found = *it;
        }
    }
    const uint8_t endpointOut = found ? dataEndpointOut(found, target.dataInterface) : 0;
    libusb_device_handle *handle = nullptr;
    const int rc = endpointOut ? libusb_open(found, &handle) : LIBUSB_ERROR_NOT_FOUND;
    libusb_free_device_list(list, 1);
    if (rc != 0) {
        ERR("NFC: can't open the probe over usbfs (%s)\n", libusb_error_name(rc));
        return nullptr;
    }
    return std::unique_ptr<UsbfsReset>(new UsbfsReset(handle, target, endpointOut));
}

UsbfsReset::UsbfsReset(void *handle, ResetTarget target, uint8_t endpointOut)
    : m_handle {handle}
    , m_target {target}
    , m_endpointOut {endpointOut}
{
}

UsbfsReset::~UsbfsReset()
{
    libusb_close(handleOf(m_handle));
}

bool UsbfsReset::send(const std::vector<uint8_t> &frame)
{
    auto *handle = handleOf(m_handle);
    std::vector<int> detached;
    const auto fail = [&](const char *what, int rc) {
        ERR("NFC: bootloader command over usbfs: %s (%s)\n", what, libusb_error_name(rc));
        for (const int iface : detached) {
            libusb_attach_kernel_driver(handle, iface); // the probe stays: give its port back
        }
        return false;
    };

    for (const int iface : {m_target.dataInterface - 1, m_target.dataInterface}) {
        if (iface < 0 || libusb_kernel_driver_active(handle, iface) != 1) {
            continue;
        }
        if (const int rc = libusb_detach_kernel_driver(handle, iface); rc != 0) {
            return fail("can't detach the kernel driver", rc);
        }
        detached.push_back(iface);
    }
    if (const int rc = libusb_claim_interface(handle, m_target.dataInterface); rc != 0) {
        return fail("can't claim the data interface", rc);
    }
    // libusb only reads an OUT buffer, though it takes a non-const one.
    int transferred = 0;
    const int rc = libusb_bulk_transfer(handle, m_endpointOut, const_cast<uint8_t *>(frame.data()),
                                        int(frame.size()), &transferred, SEND_TIMEOUT_MS);
    libusb_release_interface(handle, m_target.dataInterface);
    if (rc != 0 || size_t(transferred) != frame.size()) {
        return fail("the write failed", rc);
    }
    // Not re-attached on success: the probe is resetting, and its new firmware enumerates afresh.
    return true;
}

#else

std::unique_ptr<UsbfsReset> UsbfsReset::open(const ResetTarget &)
{
    return nullptr;
}

UsbfsReset::UsbfsReset(void *handle, ResetTarget target, uint8_t endpointOut)
    : m_handle {handle}
    , m_target {target}
    , m_endpointOut {endpointOut}
{
}

UsbfsReset::~UsbfsReset()
{
    // Never constructed off Linux; keeps the fields used for -Wunused-private-field.
    (void)m_handle;
    (void)m_target;
    (void)m_endpointOut;
}

bool UsbfsReset::send(const std::vector<uint8_t> &)
{
    return false;
}

#endif

} // namespace nfc::usb
