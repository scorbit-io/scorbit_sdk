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

// Writes a tap pad's flash through the RP2040 boot ROM's PICOBOOT interface (SB-6046).
//
// A pad in BOOTSEL enumerates as 2e8a:0003 with a mass-storage interface (RPI-RP2) and a
// vendor-class PICOBOOT interface. Hosts without USB mass storage support never get the
// RPI-RP2 drive, so the SDK writes flash over PICOBOOT on every host instead: no mount, no
// fstab entry and no root, only access to the USB device node.

#include "nfc/Uf2.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace nfc::picoboot {

constexpr uint16_t kBootselVid = 0x2E8A;
constexpr uint16_t kBootselPid = 0x0003;
constexpr uint8_t kInterfaceClass = 0xFF;

constexpr uint32_t kCommandMagic = 0x431FD10B;
constexpr size_t kCommandSize = 32;
constexpr size_t kCommandArgsSize = 16;

enum class CommandId : uint8_t {
    ExclusiveAccess = 0x01,
    Reboot = 0x02,
    FlashErase = 0x03,
    Write = 0x05,
    ExitXip = 0x06,
    Read = 0x84,
};

constexpr uint8_t kExclusive = 1;
constexpr uint32_t kSectorSize = 4096;
constexpr uint32_t kTransferChunk = 4096;
constexpr uint32_t kRebootDelayMs = 100;

/** Outcome of one USB transfer, as the writer needs to tell them apart. */
enum class UsbStatus {
    Ok,
    Timeout,
    /** The device left the bus (unplugged, reset, or rebooted). */
    NoDevice,
    /** The device stalled the endpoint: PICOBOOT's way of refusing a command. */
    Stall,
    Error,
};

/** A claimed PICOBOOT interface. The libusb implementation is in PicobootUsb.h. */
class Transport
{
public:
    virtual ~Transport() = default;

    /** PICOBOOT_IF_RESET: abandons any half-done command on the interface. */
    virtual UsbStatus resetInterface() = 0;
    /** Sends all of @p size bytes; size 0 sends a zero-length packet. */
    virtual UsbStatus bulkOut(const uint8_t *data, size_t size, unsigned timeoutMs) = 0;
    /** Receives up to @p size bytes; @p received is how many arrived (0 for a ZLP). */
    virtual UsbStatus bulkIn(uint8_t *data, size_t size, size_t &received, unsigned timeoutMs) = 0;
};

struct UsbEndpointInfo {
    uint8_t address {0};
    uint8_t attributes {0};
};

struct UsbInterfaceInfo {
    uint8_t number {0};
    uint8_t interfaceClass {0};
    std::vector<UsbEndpointInfo> endpoints;
};

struct InterfaceSelection {
    uint8_t number {0};
    uint8_t endpointOut {0};
    uint8_t endpointIn {0};
};

/** The vendor-class interface with one bulk OUT and one bulk IN endpoint, if any. */
std::optional<InterfaceSelection> findInterface(const std::vector<UsbInterfaceInfo> &interfaces);

enum class Step {
    Validate,
    InterfaceReset,
    ExclusiveAccess,
    ExitXip,
    Erase,
    Write,
    Verify,
    Reboot,
};

enum class Error {
    None,
    /** The image cannot be written as given (empty, misaligned, outside flash). */
    BadImage,
    /** The device stalled a command or answered out of protocol. */
    Refused,
    DeviceLost,
    Timeout,
    TransferFailed,
    /** Flash read back differs from the image. The pad stays in BOOTSEL. */
    VerifyMismatch,
};

const char *stepName(Step step);
const char *errorName(Error error);

struct Result {
    Error error {Error::None};
    Step step {Step::Validate};
    /** Flash address the failing step was working on (the first differing byte on a mismatch). */
    uint32_t address {0};

    bool ok() const { return error == Error::None; }
};

/**
 * Erases, writes and reads back @p image, then reboots the pad into it.
 *
 * Stops at the first failure without rebooting, so a pad with a partial or unverified image
 * stays in BOOTSEL, where the boot ROM keeps it reachable for another attempt.
 */
Result flash(Transport &transport, const uf2::FlashImage &image);

} // namespace nfc::picoboot
