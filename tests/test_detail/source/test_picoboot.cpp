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

#include "fake_bootrom.h"

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
using nfc::testing::FakeBootrom;
using nfc::testing::makeUf2;
using nfc::testing::pattern;
using nfc::testing::put32;

namespace {

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
