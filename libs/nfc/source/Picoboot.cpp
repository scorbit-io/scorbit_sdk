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

#include "nfc/Picoboot.h"

#include <algorithm>
#include <array>
#include <initializer_list>

namespace nfc::picoboot {

namespace {

constexpr unsigned kCommandTimeoutMs = 3000;
// Worst-case 4 KiB sector erase on the SPI flash parts RP2040 boards use is ~400 ms.
constexpr unsigned kEraseTimeoutPerSectorMs = 400;
constexpr uint32_t kFlashEnd = uf2::kFlashStart + 16 * 1024 * 1024;

constexpr uint8_t kEndpointDirIn = 0x80;
constexpr uint8_t kTransferTypeMask = 0x03;
constexpr uint8_t kTransferTypeBulk = 0x02;

void put32(uint8_t *p, uint32_t v)
{
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

Error toError(UsbStatus status)
{
    switch (status) {
    case UsbStatus::Ok:
        return Error::None;
    case UsbStatus::Timeout:
        return Error::Timeout;
    case UsbStatus::NoDevice:
        return Error::DeviceLost;
    case UsbStatus::Stall:
        return Error::Refused;
    case UsbStatus::Error:
        break;
    }
    return Error::TransferFailed;
}

// One PICOBOOT command: the 32-byte header, an optional data phase, then a zero-length status
// packet in the direction opposite to the data (IN when there was no data or OUT data).
class Session
{
public:
    explicit Session(Transport &transport)
        : m_transport(transport)
    {
    }

    Error command(CommandId id, std::initializer_list<uint32_t> args, size_t argsSize,
                  unsigned timeoutMs = kCommandTimeoutMs)
    {
        return run(id, args, argsSize, 0, nullptr, nullptr, timeoutMs);
    }

    Error write(uint32_t address, const uint8_t *data, uint32_t size)
    {
        return run(CommandId::Write, {address, size}, 8, size, data, nullptr, kCommandTimeoutMs);
    }

    Error read(uint32_t address, uint8_t *data, uint32_t size)
    {
        return run(CommandId::Read, {address, size}, 8, size, nullptr, data, kCommandTimeoutMs);
    }

private:
    Error run(CommandId id, std::initializer_list<uint32_t> args, size_t argsSize,
              uint32_t transferLength, const uint8_t *out, uint8_t *in, unsigned timeoutMs)
    {
        std::array<uint8_t, kCommandSize> cmd {};
        put32(&cmd[0], kCommandMagic);
        put32(&cmd[4], ++m_token);
        cmd[8] = uint8_t(id);
        cmd[9] = uint8_t(argsSize);
        put32(&cmd[12], transferLength);
        // Little-endian words; a 1-byte argument (EXCLUSIVE_ACCESS) is the first byte of its word.
        size_t offset = 16;
        for (uint32_t arg : args) {
            put32(&cmd[offset], arg);
            offset += 4;
        }

        if (auto e = toError(m_transport.bulkOut(cmd.data(), cmd.size(), timeoutMs));
            e != Error::None) {
            return e;
        }

        const bool dataIn = (uint8_t(id) & kEndpointDirIn) != 0;
        if (dataIn) {
            size_t done = 0;
            while (done < transferLength) {
                size_t received = 0;
                const auto status =
                        m_transport.bulkIn(in + done, transferLength - done, received, timeoutMs);
                if (status != UsbStatus::Ok) {
                    return toError(status);
                }
                if (received == 0) {
                    return Error::Refused;
                }
                done += received;
            }
            return toError(m_transport.bulkOut(nullptr, 0, timeoutMs));
        }

        if (transferLength > 0) {
            if (auto e = toError(m_transport.bulkOut(out, transferLength, timeoutMs));
                e != Error::None) {
                return e;
            }
        }
        std::array<uint8_t, 64> ack {};
        size_t received = 0;
        if (auto e = toError(m_transport.bulkIn(ack.data(), ack.size(), received, timeoutMs));
            e != Error::None) {
            return e;
        }
        return received == 0 ? Error::None : Error::Refused;
    }

    Transport &m_transport;
    uint32_t m_token {0};
};

Result fail(Error error, Step step, uint32_t address = 0)
{
    return Result {error, step, address};
}

} // namespace

std::optional<InterfaceSelection> findInterface(const std::vector<UsbInterfaceInfo> &interfaces)
{
    for (const auto &intf : interfaces) {
        if (intf.interfaceClass != kInterfaceClass) {
            continue;
        }
        InterfaceSelection selection;
        selection.number = intf.number;
        for (const auto &ep : intf.endpoints) {
            if ((ep.attributes & kTransferTypeMask) != kTransferTypeBulk) {
                continue;
            }
            (ep.address & kEndpointDirIn ? selection.endpointIn : selection.endpointOut) =
                    ep.address;
        }
        if (selection.endpointIn && selection.endpointOut) {
            return selection;
        }
    }
    return std::nullopt;
}

const char *stepName(Step step)
{
    switch (step) {
    case Step::Validate:
        return "validate";
    case Step::InterfaceReset:
        return "interface_reset";
    case Step::ExclusiveAccess:
        return "exclusive_access";
    case Step::ExitXip:
        return "exit_xip";
    case Step::Erase:
        return "erase";
    case Step::Write:
        return "write";
    case Step::Verify:
        return "verify";
    case Step::Reboot:
        return "reboot";
    }
    return "unknown";
}

const char *errorName(Error error)
{
    switch (error) {
    case Error::None:
        return "none";
    case Error::BadImage:
        return "bad_image";
    case Error::Refused:
        return "refused";
    case Error::DeviceLost:
        return "device_lost";
    case Error::Timeout:
        return "timeout";
    case Error::TransferFailed:
        return "transfer_failed";
    case Error::VerifyMismatch:
        return "verify_mismatch";
    }
    return "unknown";
}

Result flash(Transport &transport, const uf2::FlashImage &image)
{
    const uint32_t start = image.address;
    if (image.data.empty() || start % kSectorSize != 0 || start < uf2::kFlashStart
        || start >= kFlashEnd || image.data.size() > kFlashEnd - start) {
        return fail(Error::BadImage, Step::Validate, start);
    }
    const auto eraseSize =
            uint32_t((image.data.size() + kSectorSize - 1) / kSectorSize * kSectorSize);
    if (eraseSize > kFlashEnd - start) {
        return fail(Error::BadImage, Step::Validate, start);
    }
    // Erased flash reads 0xFF, so padding the last sector keeps the read-back comparison exact.
    std::vector<uint8_t> padded(image.data);
    padded.resize(eraseSize, 0xFF);

    if (auto e = toError(transport.resetInterface()); e != Error::None) {
        return fail(e, Step::InterfaceReset);
    }

    Session session(transport);
    if (auto e = session.command(CommandId::ExclusiveAccess, {kExclusive}, 1); e != Error::None) {
        return fail(e, Step::ExclusiveAccess);
    }
    if (auto e = session.command(CommandId::ExitXip, {}, 0); e != Error::None) {
        return fail(e, Step::ExitXip);
    }
    const unsigned eraseTimeout =
            kCommandTimeoutMs + (eraseSize / kSectorSize) * kEraseTimeoutPerSectorMs;
    if (auto e = session.command(CommandId::FlashErase, {start, eraseSize}, 8, eraseTimeout);
        e != Error::None) {
        return fail(e, Step::Erase, start);
    }

    for (uint32_t offset = 0; offset < eraseSize; offset += kTransferChunk) {
        const uint32_t size = std::min(kTransferChunk, eraseSize - offset);
        if (auto e = session.write(start + offset, padded.data() + offset, size);
            e != Error::None) {
            return fail(e, Step::Write, start + offset);
        }
    }

    std::vector<uint8_t> back(kTransferChunk);
    for (uint32_t offset = 0; offset < eraseSize; offset += kTransferChunk) {
        const uint32_t size = std::min(kTransferChunk, eraseSize - offset);
        if (auto e = session.read(start + offset, back.data(), size); e != Error::None) {
            return fail(e, Step::Verify, start + offset);
        }
        const auto first = padded.begin() + offset;
        const auto diff = std::mismatch(first, first + size, back.begin());
        if (diff.first != first + size) {
            return fail(Error::VerifyMismatch, Step::Verify,
                        start + offset + uint32_t(diff.first - first));
        }
    }

    // pc = 0 and sp = 0 ask the boot ROM for a normal boot from flash.
    if (auto e = session.command(CommandId::Reboot, {0, 0, kRebootDelayMs}, 12); e != Error::None) {
        return fail(e, Step::Reboot);
    }
    return {};
}

} // namespace nfc::picoboot
