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

// A fake RP2040 boot ROM for PICOBOOT tests (SB-6046), shared by the writer's and the SDK
// flash routine's tests.

#pragma once

#include <nfc/Picoboot.h>
#include <nfc/Uf2.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nfc::testing {

using picoboot::CommandId;
using picoboot::UsbStatus;

inline uint32_t get32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

inline void put32(std::vector<uint8_t> &out, size_t at, uint32_t v)
{
    out[at] = uint8_t(v);
    out[at + 1] = uint8_t(v >> 8);
    out[at + 2] = uint8_t(v >> 16);
    out[at + 3] = uint8_t(v >> 24);
}

inline std::vector<uint8_t> pattern(size_t size, uint8_t seed = 1)
{
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = uint8_t(i * 7 + seed);
    }
    return data;
}

// A UF2 as elf2uf2 writes one for an RP2040: 256-byte payloads, in order, from flash start.
inline std::vector<uint8_t> makeUf2(const std::vector<uint8_t> &payload)
{
    const size_t blocks = (payload.size() + 255) / 256;
    std::vector<uint8_t> out(blocks * uf2::kBlockSize, 0);
    for (size_t i = 0; i < blocks; ++i) {
        const size_t at = i * uf2::kBlockSize;
        put32(out, at, uf2::kMagicStart0);
        put32(out, at + 4, uf2::kMagicStart1);
        put32(out, at + 8, uf2::kFlagFamilyIdPresent);
        put32(out, at + 12, uf2::kFlashStart + uint32_t(i * 256));
        put32(out, at + 16, 256);
        put32(out, at + 20, uint32_t(i));
        put32(out, at + 24, uint32_t(blocks));
        put32(out, at + 28, uf2::kRp2040FamilyId);
        const size_t n = std::min<size_t>(256, payload.size() - i * 256);
        std::copy_n(payload.begin() + i * 256, n, out.begin() + at + 32);
        put32(out, at + 508, uf2::kMagicEnd);
    }
    return out;
}

// The RP2040 boot ROM's PICOBOOT interface over 2 MiB of NOR flash. Erase sets 0xFF and a write
// can only clear bits, as on the real part. Every protocol slip is recorded, so a test fails on a
// wrong phase, token or argument even when the writer happens to report success.
class FakeBootrom : public picoboot::Transport
{
public:
    static constexpr uint32_t kFlashSize = 2 * 1024 * 1024;

    std::vector<uint8_t> flash = std::vector<uint8_t>(kFlashSize, 0x5A);
    std::vector<std::string> violations;
    std::vector<CommandId> commands;
    int resets = 0;
    bool exclusive = false;
    bool xipExited = false;
    bool rebooted = false;
    bool gone = false;

    // Faults
    int vanishOnWrite = 0; // leave the bus when the Nth WRITE command arrives (1-based)
    uint32_t corruptReadAt = 0;
    std::optional<CommandId> stall;
    // Stall the status phase instead, where a boot ROM reports a command it ran and rejected.
    std::optional<CommandId> stallStatusOf;
    bool failReset = false;

    UsbStatus resetInterface() override
    {
        if (gone || failReset) {
            return UsbStatus::NoDevice;
        }
        ++resets;
        m_phase = Phase::Idle;
        return UsbStatus::Ok;
    }

    UsbStatus bulkOut(const uint8_t *data, size_t size, unsigned) override
    {
        if (gone) {
            return UsbStatus::NoDevice;
        }
        switch (m_phase) {
        case Phase::Idle:
            return onCommand(data, size);
        case Phase::DataOut: {
            if (size > m_remaining) {
                return violation("write data longer than announced");
            }
            for (size_t i = 0; i < size; ++i) {
                flash[m_address - uf2::kFlashStart + i] &= data[i];
            }
            m_address += uint32_t(size);
            m_remaining -= uint32_t(size);
            if (m_remaining == 0) {
                m_phase = Phase::StatusIn;
            }
            return UsbStatus::Ok;
        }
        case Phase::StatusOut:
            if (size != 0) {
                return violation("status OUT is not a zero-length packet");
            }
            m_phase = Phase::Idle;
            return UsbStatus::Ok;
        default:
            return violation("OUT transfer in the wrong phase");
        }
    }

    UsbStatus bulkIn(uint8_t *data, size_t size, size_t &received, unsigned) override
    {
        received = 0;
        if (gone) {
            return UsbStatus::NoDevice;
        }
        if (m_phase == Phase::DataIn) {
            const size_t n = std::min<size_t>(size, std::min<uint32_t>(m_remaining, 64));
            for (size_t i = 0; i < n; ++i) {
                const uint32_t addr = m_address + uint32_t(i);
                data[i] = flash[addr - uf2::kFlashStart] ^ (addr == corruptReadAt ? 0x01 : 0x00);
            }
            m_address += uint32_t(n);
            m_remaining -= uint32_t(n);
            received = n;
            if (m_remaining == 0) {
                m_phase = Phase::StatusOut;
            }
            return UsbStatus::Ok;
        }
        if (m_phase == Phase::StatusIn) {
            m_phase = Phase::Idle;
            if (stallStatusOf && commands.back() == *stallStatusOf) {
                return UsbStatus::Stall;
            }
            if (rebooted) {
                gone = true;
            }
            return UsbStatus::Ok;
        }
        return violation("IN transfer in the wrong phase");
    }

    size_t count(CommandId id) const
    {
        return size_t(std::count(commands.begin(), commands.end(), id));
    }

private:
    enum class Phase { Idle, DataOut, DataIn, StatusIn, StatusOut };

    UsbStatus violation(const std::string &what)
    {
        violations.push_back(what);
        return UsbStatus::Error;
    }

    bool inFlash(uint32_t address, uint32_t size) const
    {
        return address >= uf2::kFlashStart && size <= kFlashSize
            && address - uf2::kFlashStart <= kFlashSize - size;
    }

    UsbStatus onCommand(const uint8_t *cmd, size_t size)
    {
        if (size != picoboot::kCommandSize || get32(cmd) != picoboot::kCommandMagic) {
            return violation("malformed command");
        }
        if (get32(cmd + 4) != m_token + 1) {
            violations.push_back("token did not increase by one");
        }
        m_token = get32(cmd + 4);
        const auto id = CommandId(cmd[8]);
        const uint8_t argsSize = cmd[9];
        const uint32_t length = get32(cmd + 12);
        const uint32_t arg0 = get32(cmd + 16);
        const uint32_t arg1 = get32(cmd + 20);
        commands.push_back(id);
        if (stall == id) {
            return UsbStatus::Stall;
        }

        auto expect = [&](uint8_t args, uint32_t transfer) {
            if (argsSize != args || length != transfer) {
                violations.push_back("bad argument or transfer size");
            }
        };
        m_phase = Phase::StatusIn;
        switch (id) {
        case CommandId::ExclusiveAccess:
            expect(1, 0);
            exclusive = cmd[16] == picoboot::kExclusive;
            break;
        case CommandId::ExitXip:
            expect(0, 0);
            xipExited = true;
            break;
        case CommandId::FlashErase:
            expect(8, 0);
            if (!exclusive || !xipExited) {
                violations.push_back("erase before exclusive access and exit XIP");
            }
            if (arg0 % 4096 || arg1 % 4096 || !inFlash(arg0, arg1)) {
                return violation("erase range not sector aligned or outside flash");
            }
            std::fill_n(flash.begin() + (arg0 - uf2::kFlashStart), arg1, uint8_t(0xFF));
            break;
        case CommandId::Write:
            expect(8, arg1);
            if (++m_writes == vanishOnWrite) {
                gone = true;
                return UsbStatus::NoDevice;
            }
            if (arg0 % 256 || !inFlash(arg0, arg1)) {
                return violation("write not page aligned or outside flash");
            }
            m_phase = Phase::DataOut;
            m_address = arg0;
            m_remaining = arg1;
            break;
        case CommandId::Read:
            expect(8, arg1);
            if (!inFlash(arg0, arg1)) {
                return violation("read outside flash");
            }
            m_phase = Phase::DataIn;
            m_address = arg0;
            m_remaining = arg1;
            break;
        case CommandId::Reboot:
            expect(12, 0);
            rebooted = arg0 == 0 && arg1 == 0;
            break;
        default:
            return violation("unknown command");
        }
        return UsbStatus::Ok;
    }

    Phase m_phase = Phase::Idle;
    uint32_t m_token = 0;
    uint32_t m_address = 0;
    uint32_t m_remaining = 0;
    int m_writes = 0;
};

} // namespace nfc::testing
