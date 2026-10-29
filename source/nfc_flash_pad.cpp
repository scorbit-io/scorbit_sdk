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

#include "nfc_flash_pad.h"

#include "net_util.h"

#include <logger/logger.h>
#include <nfc/PicobootUsb.h>
#include <nfc/UsbPort.h>
#include <nfc/probes_manager.h>
#include <tpm/tappad_lock.h>

#if defined(__linux__)
#    include <unistd.h>
#endif

namespace scorbit {
namespace detail {
namespace nfc_flash {

namespace {

constexpr uint16_t SCORBIT_PROBE_VID = 0xCAFE;

class SdkHold : public Pad::Hold
{
public:
    SdkHold(std::unique_ptr<TappadLock> lock, std::unique_ptr<nfc::ProbesManager::FlashHold> probe)
        : m_lock {std::move(lock)}
        , m_probe {std::move(probe)}
    {
    }

    std::optional<std::string> port() override
    {
        return m_probe ? nfc::usb::portOfDevice(m_probe->devicePath()) : std::nullopt;
    }

    void enterBootloader() override
    {
        if (m_probe) {
            m_probe->enterBootloader();
            m_probe.reset(); // the probe is gone; keep only the tap pad lock
        }
    }

private:
    std::unique_ptr<TappadLock> m_lock;
    std::unique_ptr<nfc::ProbesManager::FlashHold> m_probe;
};

} // namespace

SdkPad::SdkPad(std::shared_ptr<nfc::ProbesManager> probes)
    : m_probes {std::move(probes)}
{
}

std::optional<uint64_t> SdkPad::build()
{
    m_probes->rescanNfc();
    const auto info = m_probes->nfcProbeInfo();
    if (info.status != nfc::NfcProbeInfoResult::Status::Ok) {
        return std::nullopt;
    }
    return nfcProbeFirmware(info.probe).timestampUtc;
}

std::unique_ptr<Pad::Hold> SdkPad::hold(bool withProbe, Refused &why)
{
    auto lock = std::make_unique<TappadLock>();
    if (!lock->ok()) {
        why = lock->status() == TappadLock::BUSY ? Refused::Busy : Refused::NoLock;
        return nullptr;
    }
    // A fallback lock lets another SDK process write the same pad and the same record.
    if (!TappadLock::isCrossProcess()) {
        ERR("NFC flash: no tap pad lock file can be opened; flashing refused");
        why = Refused::NoLock;
        return nullptr;
    }
    std::unique_ptr<nfc::ProbesManager::FlashHold> probe;
    if (withProbe) {
        probe = m_probes->holdNfcForFlash();
        if (!probe) {
            // Present but held by another flash, or not present
            why = m_probes->hasNfc() ? Refused::Busy : Refused::NoProbe;
            return nullptr;
        }
    }
    return std::make_unique<SdkHold>(std::move(lock), std::move(probe));
}

std::vector<std::string> SdkPad::bootselPorts()
{
    std::vector<std::string> ports;
    for (const auto &device : nfc::picoboot::listBootselDevices()) {
        ports.push_back(device.port);
    }
    return ports;
}

bool SdkPad::probeAt(const std::string &port)
{
    const auto device = nfc::usb::deviceAtPort(port);
    return device && device->vendorId == SCORBIT_PROBE_VID;
}

std::unique_ptr<nfc::picoboot::Transport>
SdkPad::openBootsel([[maybe_unused]] const std::string &port)
{
#if defined(__linux__)
    for (const auto &device : nfc::picoboot::listBootselDevices()) {
        if (device.port != port) {
            continue;
        }
        nfc::picoboot::OpenError error {};
        auto transport = nfc::picoboot::LibusbTransport::open(device, error);
        if (!transport) {
            WRN("NFC flash: PICOBOOT at {} not opened: {}", port,
                nfc::picoboot::openErrorName(error));
        }
        return transport;
    }
#endif
    return nullptr;
}

bool nfcFlashable([[maybe_unused]] nfc::ProbesManager &probes)
{
#if defined(__linux__)
    const auto port = nfc::usb::portOfDevice(probes.nfcDevicePath());
    if (!port) {
        return false;
    }
    const auto device = nfc::usb::deviceAtPort(*port);
    if (!device || ::access(nfc::usb::deviceNode(*device).c_str(), R_OK | W_OK) != 0) {
        return false;
    }
    // Every flash is refused without a cross-process lock, so don't advertise one. Busy means
    // another holder has the lock file, so one can be had.
    constexpr uint32_t LOCK_PROBE_MS = 100;
    const TappadLock lock {LOCK_PROBE_MS};
    return lock.ok() ? TappadLock::isCrossProcess() : lock.status() == TappadLock::BUSY;
#else
    return false;
#endif
}

} // namespace nfc_flash
} // namespace detail
} // namespace scorbit
