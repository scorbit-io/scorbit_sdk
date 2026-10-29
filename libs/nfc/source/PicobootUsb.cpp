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

#include "nfc/PicobootUsb.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <system_error>

#if defined(__linux__)
// ListUsbDevices.h logs through Util.h's ERR and brings libusb and the shared libusb context.
#    include "nfc/Util.h"
#    include "nfc/ListUsbDevices.h"
#endif

namespace nfc::picoboot {

namespace {

std::optional<unsigned> readNumber(const std::filesystem::path &file, int base)
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

} // namespace

std::vector<BootselDevice> listBootselDevices(const std::filesystem::path &sysfsDevices)
{
    std::vector<BootselDevice> devices;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(sysfsDevices, ec), end; !ec && it != end;
         it.increment(ec)) {
        const auto &dir = it->path();
        // Interface entries ("1-1:1.0") and root hubs have no matching ids and drop out here.
        if (readNumber(dir / "idVendor", 16) != kBootselVid
            || readNumber(dir / "idProduct", 16) != kBootselPid) {
            continue;
        }
        const auto bus = readNumber(dir / "busnum", 10);
        const auto address = readNumber(dir / "devnum", 10);
        if (!bus || !address) {
            continue;
        }
        devices.push_back({dir.filename().string(), *bus, *address});
    }
    std::sort(devices.begin(), devices.end(),
              [](const BootselDevice &a, const BootselDevice &b) { return a.port < b.port; });
    return devices;
}

#if defined(__linux__)

namespace {

constexpr unsigned kControlTimeoutMs = 1000;
constexpr uint8_t kResetRequestType = 0x41; // vendor, interface, host to device
constexpr uint8_t kResetRequest = 0x41;

UsbStatus toStatus(int rc)
{
    switch (rc) {
    case LIBUSB_SUCCESS:
        return UsbStatus::Ok;
    case LIBUSB_ERROR_TIMEOUT:
        return UsbStatus::Timeout;
    case LIBUSB_ERROR_NO_DEVICE:
        return UsbStatus::NoDevice;
    case LIBUSB_ERROR_PIPE:
        return UsbStatus::Stall;
    default:
        return UsbStatus::Error;
    }
}

OpenError toOpenError(int rc)
{
    switch (rc) {
    case LIBUSB_ERROR_ACCESS:
        return OpenError::AccessDenied;
    case LIBUSB_ERROR_NO_DEVICE:
    case LIBUSB_ERROR_NOT_FOUND:
        return OpenError::NotFound;
    case LIBUSB_ERROR_BUSY:
        return OpenError::Busy;
    default:
        return OpenError::Failed;
    }
}

std::vector<UsbInterfaceInfo> interfacesOf(const libusb_config_descriptor &cfg)
{
    std::vector<UsbInterfaceInfo> interfaces;
    for (int i = 0; i < cfg.bNumInterfaces; ++i) {
        if (cfg.interface[i].num_altsetting < 1) {
            continue;
        }
        const libusb_interface_descriptor &alt = cfg.interface[i].altsetting[0];
        UsbInterfaceInfo info;
        info.number = alt.bInterfaceNumber;
        info.interfaceClass = alt.bInterfaceClass;
        for (int e = 0; e < alt.bNumEndpoints; ++e) {
            info.endpoints.push_back(
                    {alt.endpoint[e].bEndpointAddress, alt.endpoint[e].bmAttributes});
        }
        interfaces.push_back(std::move(info));
    }
    return interfaces;
}

libusb_device_handle *handleOf(void *handle)
{
    return static_cast<libusb_device_handle *>(handle);
}

} // namespace

const char *openErrorName(OpenError error)
{
    switch (error) {
    case OpenError::None:
        return "none";
    case OpenError::NotFound:
        return "not_found";
    case OpenError::NoInterface:
        return "no_interface";
    case OpenError::AccessDenied:
        return "access_denied";
    case OpenError::Busy:
        return "busy";
    case OpenError::Failed:
        return "failed";
    }
    return "unknown";
}

std::unique_ptr<LibusbTransport> LibusbTransport::open(const BootselDevice &device,
                                                       OpenError &error)
{
    error = OpenError::Failed;
    libusb_context *ctx = probeLibusbContext();
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
        if (libusb_get_bus_number(list[i]) == device.bus
            && libusb_get_device_address(list[i]) == device.address
            && libusb_get_device_descriptor(list[i], &desc) == 0 && desc.idVendor == kBootselVid
            && desc.idProduct == kBootselPid) {
            found = list[i];
            break;
        }
    }
    if (!found) {
        libusb_free_device_list(list, 1);
        error = OpenError::NotFound;
        return nullptr;
    }

    libusb_config_descriptor *cfg = nullptr;
    if (int rc = libusb_get_active_config_descriptor(found, &cfg); rc != 0) {
        libusb_free_device_list(list, 1);
        error = toOpenError(rc);
        return nullptr;
    }
    const auto selection = findInterface(interfacesOf(*cfg));
    libusb_free_config_descriptor(cfg);
    if (!selection) {
        libusb_free_device_list(list, 1);
        error = OpenError::NoInterface;
        return nullptr;
    }

    libusb_device_handle *handle = nullptr;
    const int openRc = libusb_open(found, &handle);
    libusb_free_device_list(list, 1);
    if (openRc != 0) {
        error = toOpenError(openRc);
        return nullptr;
    }
    // The boot ROM's vendor interface normally has no driver; detach one if a host bound it.
    if (libusb_kernel_driver_active(handle, selection->number) == 1) {
        libusb_detach_kernel_driver(handle, selection->number);
    }
    if (int rc = libusb_claim_interface(handle, selection->number); rc != 0) {
        libusb_close(handle);
        error = toOpenError(rc);
        return nullptr;
    }
    error = OpenError::None;
    return std::unique_ptr<LibusbTransport>(new LibusbTransport(handle, *selection));
}

LibusbTransport::LibusbTransport(void *handle, InterfaceSelection selection)
    : m_handle(handle)
    , m_interface(selection)
{
}

LibusbTransport::~LibusbTransport()
{
    libusb_release_interface(handleOf(m_handle), m_interface.number);
    libusb_close(handleOf(m_handle));
}

UsbStatus LibusbTransport::resetInterface()
{
    // A refused command leaves both bulk endpoints halted; clear them as picotool's reset does.
    libusb_clear_halt(handleOf(m_handle), m_interface.endpointIn);
    libusb_clear_halt(handleOf(m_handle), m_interface.endpointOut);
    const int rc = libusb_control_transfer(handleOf(m_handle), kResetRequestType, kResetRequest, 0,
                                           m_interface.number, nullptr, 0, kControlTimeoutMs);
    return rc < 0 ? toStatus(rc) : UsbStatus::Ok;
}

UsbStatus LibusbTransport::bulkOut(const uint8_t *data, size_t size, unsigned timeoutMs)
{
    // libusb takes a non-const buffer even for OUT; a ZLP still needs a valid pointer.
    uint8_t zlp = 0;
    auto *buffer = size ? const_cast<uint8_t *>(data) : &zlp;
    int transferred = 0;
    const int rc = libusb_bulk_transfer(handleOf(m_handle), m_interface.endpointOut, buffer,
                                        int(size), &transferred, timeoutMs);
    if (rc != 0) {
        return toStatus(rc);
    }
    return size_t(transferred) == size ? UsbStatus::Ok : UsbStatus::Error;
}

UsbStatus LibusbTransport::bulkIn(uint8_t *data, size_t size, size_t &received, unsigned timeoutMs)
{
    int transferred = 0;
    const int rc = libusb_bulk_transfer(handleOf(m_handle), m_interface.endpointIn, data, int(size),
                                        &transferred, timeoutMs);
    received = transferred > 0 ? size_t(transferred) : 0;
    return toStatus(rc);
}

#endif

} // namespace nfc::picoboot
