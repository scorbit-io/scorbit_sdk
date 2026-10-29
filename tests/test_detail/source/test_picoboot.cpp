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

// SB-6046: UF2 validation and the PICOBOOT writer, against a fake boot ROM.

#include <nfc/Picoboot.h>
#include <nfc/PicobootUsb.h>
#include <nfc/Uf2.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace nfc;
using picoboot::CommandId;
using picoboot::UsbStatus;

namespace {

void put32(std::vector<uint8_t> &out, size_t at, uint32_t v)
{
    out[at] = uint8_t(v);
    out[at + 1] = uint8_t(v >> 8);
    out[at + 2] = uint8_t(v >> 16);
    out[at + 3] = uint8_t(v >> 24);
}

uint32_t get32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

std::vector<uint8_t> pattern(size_t size, uint8_t seed = 1)
{
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; ++i) {
        data[i] = uint8_t(i * 7 + seed);
    }
    return data;
}

// A UF2 as elf2uf2 writes one for an RP2040: 256-byte payloads, in order, from flash start.
std::vector<uint8_t> makeUf2(const std::vector<uint8_t> &payload)
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

uf2::ParseResult parseBytes(const std::vector<uint8_t> &bytes, size_t max = uf2::kMaxImageBytes)
{
    return uf2::parse(bytes.data(), bytes.size(), max);
}

std::filesystem::path makeTempDir(const std::string &tag)
{
    std::random_device rd;
    auto dir = std::filesystem::temp_directory_path()
             / ("scorbit-picoboot-" + tag + "-" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    return dir;
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

uf2::FlashImage imageOf(size_t size)
{
    return uf2::FlashImage {uf2::kFlashStart, pattern(size)};
}

} // namespace

TEST_CASE("uf2: a valid RP2040 image parses to contiguous flash data", "[nfc][uf2]")
{
    const auto payload = pattern(256 * 5);
    const auto result = parseBytes(makeUf2(payload));
    REQUIRE(result.ok());
    CHECK(result.image.address == uf2::kFlashStart);
    CHECK(result.image.data == payload);
}

TEST_CASE("uf2: malformed files are refused with the reason", "[nfc][uf2]")
{
    const auto good = makeUf2(pattern(256 * 4));

    SECTION("empty")
    {
        CHECK(parseBytes({}).error == uf2::Error::Empty);
    }
    SECTION("not a whole number of blocks")
    {
        auto bytes = good;
        bytes.pop_back();
        CHECK(parseBytes(bytes).error == uf2::Error::NotBlockAligned);
    }
    SECTION("bad start magic")
    {
        auto bytes = good;
        put32(bytes, 512 * 2, 0);
        const auto result = parseBytes(bytes);
        CHECK(result.error == uf2::Error::BadMagic);
        CHECK(result.block == 2);
    }
    SECTION("bad end magic")
    {
        auto bytes = good;
        put32(bytes, 512 + 508, 0);
        CHECK(parseBytes(bytes).error == uf2::Error::BadMagic);
    }
    SECTION("another chip family")
    {
        auto bytes = good;
        put32(bytes, 28, 0x68ED2B88); // SAMD21
        CHECK(parseBytes(bytes).error == uf2::Error::WrongFamily);
    }
    SECTION("no family id")
    {
        auto bytes = good;
        put32(bytes, 8, 0);
        CHECK(parseBytes(bytes).error == uf2::Error::WrongFamily);
    }
    SECTION("a block not meant for main flash")
    {
        auto bytes = good;
        put32(bytes, 512 + 8, uf2::kFlagFamilyIdPresent | uf2::kFlagNotMainFlash);
        CHECK(parseBytes(bytes).error == uf2::Error::NotMainFlash);
    }
    SECTION("payload other than 256 bytes")
    {
        auto bytes = good;
        put32(bytes, 16, 476);
        CHECK(parseBytes(bytes).error == uf2::Error::BadPayloadSize);
    }
    SECTION("blocks out of order")
    {
        auto bytes = good;
        put32(bytes, 512 + 20, 2);
        CHECK(parseBytes(bytes).error == uf2::Error::BadBlockNumber);
    }
    SECTION("block count disagrees with the file")
    {
        auto bytes = good;
        put32(bytes, 24, 9);
        CHECK(parseBytes(bytes).error == uf2::Error::BadBlockNumber);
    }
    SECTION("image not at the start of flash")
    {
        auto bytes = good;
        for (size_t i = 0; i < 4; ++i) {
            put32(bytes, i * 512 + 12, 0x20000000 + uint32_t(i * 256));
        }
        CHECK(parseBytes(bytes).error == uf2::Error::NotAtFlashStart);
    }
    SECTION("a gap in the image")
    {
        auto bytes = good;
        put32(bytes, 512 * 3 + 12, uf2::kFlashStart + 256 * 4);
        const auto result = parseBytes(bytes);
        CHECK(result.error == uf2::Error::NotContiguous);
        CHECK(result.block == 3);
    }
    SECTION("larger than the cap")
    {
        CHECK(parseBytes(good, 256 * 3).error == uf2::Error::TooLarge);
        CHECK(parseBytes(good, 256 * 4).ok());
    }
}

TEST_CASE("uf2: parseFile reads a file and refuses a missing one", "[nfc][uf2]")
{
    const auto dir = makeTempDir("uf2");
    const auto payload = pattern(256 * 3, 9);
    const auto bytes = makeUf2(payload);
    {
        std::ofstream out(dir / "nfc.uf2", std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    }

    const auto result = uf2::parseFile(dir / "nfc.uf2");
    CHECK(result.ok());
    CHECK(result.image.data == payload);
    CHECK(uf2::parseFile(dir / "missing.uf2").error == uf2::Error::Unreadable);
    CHECK(uf2::parseFile(dir / "nfc.uf2", 256 * 2).error == uf2::Error::TooLarge);

    std::filesystem::remove_all(dir);
}

TEST_CASE("picoboot: erases, writes, verifies and reboots", "[nfc][picoboot]")
{
    FakeBootrom rom;
    // Not a whole number of sectors, so the last one is padded with erased bytes.
    const auto image = imageOf(3 * 4096 + 300);

    const auto result = picoboot::flash(rom, image);

    CHECK(rom.violations.empty());
    REQUIRE(result.ok());
    CHECK(rom.resets == 1);
    CHECK(rom.rebooted);
    CHECK(rom.count(CommandId::FlashErase) == 1);
    CHECK(rom.count(CommandId::Write) == 4);
    CHECK(rom.count(CommandId::Read) == 4);
    CHECK(rom.commands.front() == CommandId::ExclusiveAccess);
    CHECK(rom.commands.back() == CommandId::Reboot);
    CHECK(std::equal(image.data.begin(), image.data.end(), rom.flash.begin()));
    CHECK(std::all_of(rom.flash.begin() + long(image.data.size()), rom.flash.begin() + 4 * 4096,
                      [](uint8_t b) { return b == 0xFF; }));
    CHECK(rom.flash[4 * 4096] == 0x5A); // past the erased range: untouched
}

TEST_CASE("picoboot: a read-back mismatch stops before reboot", "[nfc][picoboot]")
{
    FakeBootrom rom;
    rom.corruptReadAt = uf2::kFlashStart + 5000;

    const auto result = picoboot::flash(rom, imageOf(3 * 4096));

    CHECK(rom.violations.empty());
    CHECK(result.error == picoboot::Error::VerifyMismatch);
    CHECK(result.step == picoboot::Step::Verify);
    CHECK(result.address == uf2::kFlashStart + 5000);
    CHECK_FALSE(rom.rebooted);
    CHECK(rom.count(CommandId::Reboot) == 0);
}

TEST_CASE("picoboot: a device that vanishes mid-write is reported lost", "[nfc][picoboot]")
{
    FakeBootrom rom;
    rom.vanishOnWrite = 3;

    const auto result = picoboot::flash(rom, imageOf(5 * 4096));

    CHECK(rom.violations.empty());
    CHECK(result.error == picoboot::Error::DeviceLost);
    CHECK(result.step == picoboot::Step::Write);
    CHECK(result.address == uf2::kFlashStart + 2 * 4096);
    CHECK(rom.count(CommandId::Read) == 0);
    CHECK_FALSE(rom.rebooted);
}

TEST_CASE("picoboot: a stalled command is reported refused at its step", "[nfc][picoboot]")
{
    FakeBootrom rom;
    rom.stall = CommandId::FlashErase;

    const auto result = picoboot::flash(rom, imageOf(4096));

    CHECK(result.error == picoboot::Error::Refused);
    CHECK(result.step == picoboot::Step::Erase);
    CHECK(rom.count(CommandId::Write) == 0);
}

TEST_CASE("picoboot: a write stalled in its status phase is refused", "[nfc][picoboot]")
{
    FakeBootrom rom;
    rom.stallStatusOf = CommandId::Write;

    const auto result = picoboot::flash(rom, imageOf(2 * 4096));

    CHECK(rom.violations.empty());
    CHECK(result.error == picoboot::Error::Refused);
    CHECK(result.step == picoboot::Step::Write);
    CHECK(result.address == uf2::kFlashStart);
    CHECK(rom.count(CommandId::Write) == 1);
    CHECK_FALSE(rom.rebooted);
}

TEST_CASE("picoboot: a failed interface reset sends no command", "[nfc][picoboot]")
{
    FakeBootrom rom;
    rom.failReset = true;

    const auto result = picoboot::flash(rom, imageOf(4096));

    CHECK(result.error == picoboot::Error::DeviceLost);
    CHECK(result.step == picoboot::Step::InterfaceReset);
    CHECK(rom.commands.empty());
}

TEST_CASE("picoboot: an image it cannot write is refused before any USB traffic", "[nfc][picoboot]")
{
    FakeBootrom rom;

    CHECK(picoboot::flash(rom, {uf2::kFlashStart, {}}).error == picoboot::Error::BadImage);
    CHECK(picoboot::flash(rom, {uf2::kFlashStart + 256, pattern(256)}).error
          == picoboot::Error::BadImage);
    CHECK(picoboot::flash(rom, {0x20000000, pattern(256)}).error == picoboot::Error::BadImage);
    CHECK(picoboot::flash(rom, {uf2::kFlashStart + 16 * 1024 * 1024 - 4096, pattern(4097)}).error
          == picoboot::Error::BadImage);
    CHECK(rom.resets == 0);
    CHECK(rom.commands.empty());
}

TEST_CASE("picoboot: finds the vendor interface of a BOOTSEL device", "[nfc][picoboot]")
{
    constexpr uint8_t bulk = 0x02;
    const picoboot::UsbInterfaceInfo massStorage {0, 0x08, {{0x81, bulk}, {0x02, bulk}}};
    const picoboot::UsbInterfaceInfo vendor {1, 0xFF, {{0x03, bulk}, {0x84, bulk}}};

    const auto found = picoboot::findInterface({massStorage, vendor});
    REQUIRE(found);
    CHECK(found->number == 1);
    CHECK(found->endpointOut == 0x03);
    CHECK(found->endpointIn == 0x84);
}

TEST_CASE("picoboot: no PICOBOOT interface is reported as none", "[nfc][picoboot]")
{
    constexpr uint8_t bulk = 0x02, interrupt = 0x03;
    const picoboot::UsbInterfaceInfo massStorage {0, 0x08, {{0x81, bulk}, {0x02, bulk}}};
    const picoboot::UsbInterfaceInfo vendorNoBulk {1, 0xFF, {{0x83, interrupt}}};

    CHECK_FALSE(picoboot::findInterface({massStorage}));
    CHECK_FALSE(picoboot::findInterface({massStorage, vendorNoBulk}));
    CHECK_FALSE(picoboot::findInterface({}));
}

TEST_CASE("picoboot: lists BOOTSEL devices by port from sysfs", "[nfc][picoboot]")
{
    const auto root = makeTempDir("sysfs");
    auto device = [&](const std::string &name, const std::string &vid, const std::string &pid,
                      unsigned bus, unsigned dev) {
        std::filesystem::create_directories(root / name);
        std::ofstream(root / name / "idVendor") << vid << "\n";
        std::ofstream(root / name / "idProduct") << pid << "\n";
        std::ofstream(root / name / "busnum") << bus << "\n";
        std::ofstream(root / name / "devnum") << dev << "\n";
    };
    device("usb1", "1d6b", "0002", 1, 1);
    device("2-1.3", "2e8a", "0003", 2, 12);
    device("1-1", "2e8a", "0003", 1, 7);
    device("1-2", "cafe", "4002", 1, 8);
#if !defined(_WIN32) // Windows file names cannot hold the colon
    std::filesystem::create_directories(root / "1-1:1.0"); // an interface: no ids
#endif

    const auto found = picoboot::listBootselDevices(root);

    REQUIRE(found.size() == 2);
    CHECK(found[0].port == "1-1");
    CHECK(found[0].bus == 1);
    CHECK(found[0].address == 7);
    CHECK(found[1].port == "2-1.3");
    CHECK(found[1].bus == 2);
    CHECK(found[1].address == 12);
    CHECK(picoboot::listBootselDevices(root / "absent").empty());

    std::filesystem::remove_all(root);
}
