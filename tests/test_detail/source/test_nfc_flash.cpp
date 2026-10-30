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

// SB-5759: the SDK's NFC tap pad flash, against a fake pad and a fake boot ROM.

#include "nfc_flash.h"

#include "fake_bootrom.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>

using namespace scorbit::detail::nfc_flash;
using nfc::testing::FakeBootrom;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

constexpr uint64_t OLD_BUILD = 20261011135327;
constexpr uint64_t NEW_BUILD = 20261025121050;
const std::string PORT = "1-1";

struct TempDir {
    TempDir()
    {
        std::random_device rd;
        path = fs::temp_directory_path() / ("scorbit-nfc-flash-" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    fs::path path;
};

class FakeHost : public Host
{
public:
    bool stopping() override { return stop; }
    bool idle() override { return idleAfterPolls <= 0; }
    bool sleepFor(std::chrono::milliseconds duration) override
    {
        clock += duration;
        --idleAfterPolls;
        return !stop;
    }
    std::chrono::system_clock::time_point now() override { return clock; }
    void stage(const std::string &runId, Stage stage) override
    {
        CHECK(runId == "run-1");
        stages.push_back(stage);
        if (stopAt == stage) {
            stop = true;
        }
    }
    bool download(const std::string &url, const fs::path &to, uint64_t maxBytes) override
    {
        CHECK(url == "https://api.example/image");
        downloadLimit = maxBytes;
        downloadedTo.push_back(to);
        if (duringDownload) {
            duringDownload();
        }
        if (failDownload) {
            return false;
        }
        std::ofstream(to, std::ios::binary)
                .write(reinterpret_cast<const char *>(image.data()), std::streamsize(image.size()));
        return true;
    }

    std::vector<uint8_t> image;
    std::function<void()> duringDownload;
    bool failDownload = false;
    bool stop = false;
    std::optional<Stage> stopAt;
    int idleAfterPolls = 0;
    std::chrono::system_clock::time_point clock {std::chrono::seconds {1'800'000'000}};
    std::vector<Stage> stages;
    uint64_t downloadLimit = 0;
    std::vector<fs::path> downloadedTo;
};

// A tap pad at PORT that drops into BOOTSEL on the bootloader command and comes back, on
// whatever the fake ROM's REBOOT leaves it on, once PICOBOOT reboots it.
class FakePad : public Pad
{
public:
    class FakeHold : public Hold
    {
    public:
        explicit FakeHold(FakePad &pad)
            : m_pad {pad}
        {
            ++m_pad.holds;
        }
        ~FakeHold() override { --m_pad.holds; }
        std::optional<std::string> port() override
        {
            return m_pad.portKnown ? std::optional {PORT} : std::nullopt;
        }
        void enterBootloader() override
        {
            ++m_pad.bootloaderCommands;
            m_pad.bootselIn = m_pad.bootselAfterPolls;
            if (m_pad.bootloaderWorks) {
                m_pad.present = false;
                m_pad.rom = std::make_shared<FakeBootrom>();
                m_pad.rom->corruptReadAt = m_pad.corruptReadAt;
            } else if (m_pad.vanishOnBootloader) {
                m_pad.present = false;
            }
        }

    private:
        FakePad &m_pad;
    };

    // Forwards to the shared ROM, which outlives a single open.
    class Link : public nfc::picoboot::Transport
    {
    public:
        explicit Link(std::shared_ptr<FakeBootrom> rom)
            : m_rom {std::move(rom)}
        {
        }
        nfc::picoboot::UsbStatus resetInterface() override { return m_rom->resetInterface(); }
        nfc::picoboot::UsbStatus bulkOut(const uint8_t *d, size_t n, unsigned t) override
        {
            return m_rom->bulkOut(d, n, t);
        }
        nfc::picoboot::UsbStatus bulkIn(uint8_t *d, size_t n, size_t &r, unsigned t) override
        {
            return m_rom->bulkIn(d, n, r, t);
        }

    private:
        std::shared_ptr<FakeBootrom> m_rom;
    };

    std::optional<uint64_t> build() override
    {
        settle();
        ++builds;
        if (present && failReads > 0) {
            --failReads;
            return std::nullopt;
        }
        return present && !failRead ? std::optional {firmware} : std::nullopt;
    }
    std::unique_ptr<Hold> hold(bool withProbe, Refused &why) override
    {
        settle();
        if (beforeHold) {
            beforeHold();
        }
        if (lockBusy || noLock || (withProbe && !present)) {
            why = lockBusy ? Refused::Busy : noLock ? Refused::NoLock : Refused::NoProbe;
            return nullptr;
        }
        return std::make_unique<FakeHold>(*this);
    }
    std::vector<std::string> bootselPorts() override
    {
        settle();
        auto ports = foreign;
        if (rom && !rom->gone) {
            ports.push_back(PORT);
        }
        return ports;
    }
    bool probeAt(const std::string &port) override
    {
        settle();
        return port == PORT && present;
    }
    std::unique_ptr<nfc::picoboot::Transport> openBootsel(const std::string &port) override
    {
        settle();
        ++opens;
        if (port != PORT || !rom || rom->gone || failOpen) {
            return nullptr;
        }
        return std::make_unique<Link>(rom);
    }

    // After PICOBOOT REBOOT the ROM leaves the bus and the pad comes back.
    void settle()
    {
        if (bootselIn > 0 && --bootselIn == 0) {
            rom = std::make_shared<FakeBootrom>(); // a late BOOTSEL after the command
            rom->corruptReadAt = corruptReadAt;
        }
        if (appearAfterPolls > 0 && --appearAfterPolls == 0) {
            present = true; // enumerated late, as after a cold boot
        }
        if (rom && rom->gone && !settled) {
            settled = true;
            present = comesBack;
            firmware = rebootBuild;
            failRead = failReadAfterReboot;
        }
    }

    uint64_t firmware = OLD_BUILD;
    uint64_t rebootBuild = NEW_BUILD;
    bool present = true;
    bool portKnown = true;
    bool failRead = false;
    int failReads = 0;
    int bootselAfterPolls = 0;
    int bootselIn = 0;
    bool failReadAfterReboot = false;
    bool lockBusy = false;
    bool noLock = false;
    bool bootloaderWorks = true;
    bool vanishOnBootloader = false;
    bool failOpen = false;
    bool comesBack = true;
    uint32_t corruptReadAt = 0;
    int appearAfterPolls = 0;
    std::vector<std::string> foreign;
    std::function<void()> beforeHold;
    std::shared_ptr<FakeBootrom> rom;
    bool settled = false;
    int bootloaderCommands = 0;
    int holds = 0;
    int opens = 0;
    int builds = 0;
};

struct Fixture {
    Fixture()
        : store {{dir.path / "state"}}
    {
        payload = nfc::testing::pattern(3 * 4096 + 100);
        host.image = nfc::testing::makeUf2(payload);
        const auto file = dir.path / "image.uf2";
        std::ofstream(file, std::ios::binary)
                .write(reinterpret_cast<const char *>(host.image.data()),
                       std::streamsize(host.image.size()));
        request.runId = "run-1";
        request.targetBuild = NEW_BUILD;
        request.sha256 = sha256File(file);
        request.size = host.image.size();
        request.imageUrl = "https://api.example/image";
    }

    Flasher flasher() { return Flasher {host, pad, store}; }
    bool recordLeft() const { return store.load().has_value(); }

    TempDir dir;
    RecordStore store;
    FakeHost host;
    FakePad pad;
    Request request;
    std::vector<uint8_t> payload;
};

bool sameOutcome(const Outcome &a, const Outcome &b)
{
    return a.status == b.status && a.stage == b.stage && a.failure == b.failure;
}

} // namespace

TEST_CASE("nfc flash: writes the pad and verifies the new build", "[nfc_flash]")
{
    Fixture f;
    const auto outcome = f.flasher().run(f.request);

    REQUIRE(outcome);
    CHECK(sameOutcome(*outcome, {Status::Succeeded, Stage::Done}));
    CHECK(f.host.stages
          == std::vector {Stage::IdleCheck, Stage::Staging, Stage::Locking, Stage::Bootloader,
                          Stage::Copy, Stage::Reboot, Stage::Verify});
    CHECK(f.host.downloadLimit == f.request.size);
    CHECK(f.pad.bootloaderCommands == 1);
    CHECK(f.pad.holds == 0);
    REQUIRE(f.pad.rom);
    CHECK(f.pad.rom->violations.empty());
    CHECK(std::equal(f.payload.begin(), f.payload.end(), f.pad.rom->flash.begin()));
    CHECK_FALSE(f.recordLeft());
    CHECK_FALSE(fs::exists(RecordStore::imagePath(f.dir.path / "state")));
}

TEST_CASE("nfc flash: a pad already on the build is left alone", "[nfc_flash]")
{
    Fixture f;
    f.pad.firmware = NEW_BUILD;
    const auto outcome = f.flasher().run(f.request);

    REQUIRE(outcome);
    CHECK(sameOutcome(*outcome, {Status::Succeeded, Stage::Done}));
    CHECK(f.host.downloadLimit == 0);
    CHECK(f.pad.bootloaderCommands == 0);
}

TEST_CASE("nfc flash: waits for idle, and defers at the deadline", "[nfc_flash]")
{
    Fixture f;
    SECTION("idle in time")
    {
        f.host.idleAfterPolls = 3;
        f.request.deadline = f.host.clock + 1h;
        const auto outcome = f.flasher().run(f.request);
        REQUIRE(outcome);
        CHECK(outcome->status == Status::Succeeded);
    }
    SECTION("never idle before the deadline")
    {
        f.host.idleAfterPolls = 1'000'000;
        f.request.deadline = f.host.clock + 1min;
        const auto outcome = f.flasher().run(f.request);
        REQUIRE(outcome);
        CHECK(sameOutcome(*outcome, {Status::Deferred, Stage::IdleCheck}));
        CHECK(f.pad.bootloaderCommands == 0);
    }
}

TEST_CASE("nfc flash: a bad download never reaches the pad", "[nfc_flash]")
{
    Fixture f;
    Outcome expected;
    SECTION("download fails")
    {
        f.host.failDownload = true;
        expected = {Status::Failed, Stage::Staging, Failure::Download};
    }
    SECTION("checksum differs")
    {
        f.request.sha256 = std::string(64, '0');
        expected = {Status::Failed, Stage::Staging, Failure::Checksum};
    }
    SECTION("size differs")
    {
        f.request.size += 512;
        expected = {Status::Failed, Stage::Staging, Failure::Checksum};
    }
    SECTION("not a tap pad image")
    {
        f.host.image = std::vector<uint8_t>(512, 0xAB);
        const auto file = f.dir.path / "junk";
        std::ofstream(file, std::ios::binary)
                .write(reinterpret_cast<const char *>(f.host.image.data()), 512);
        f.request.sha256 = sha256File(file);
        f.request.size = 512;
        expected = {Status::Failed, Stage::Staging, Failure::InvalidPolicy};
    }
    const auto outcome = f.flasher().run(f.request);
    REQUIRE(outcome);
    CHECK(sameOutcome(*outcome, expected));
    CHECK(f.pad.bootloaderCommands == 0);
    CHECK_FALSE(f.recordLeft());
}

TEST_CASE("nfc flash: refuses before the bootloader command", "[nfc_flash]")
{
    Fixture f;
    Outcome expected;
    SECTION("tap pad lock held elsewhere")
    {
        f.pad.lockBusy = true;
        expected = {Status::Failed, Stage::Locking, Failure::Busy};
    }
    SECTION("no lock that excludes other processes")
    {
        f.pad.noLock = true;
        expected = {Status::Failed, Stage::Locking, Failure::Internal};
    }
    SECTION("another device already in BOOTSEL")
    {
        f.pad.foreign = {"1-2"};
        expected = {Status::Failed, Stage::Locking, Failure::ForeignBootloader};
    }
    SECTION("the pad's port cannot be told")
    {
        f.pad.portKnown = false;
        expected = {Status::Failed, Stage::Locking, Failure::Internal};
    }
    const auto outcome = f.flasher().run(f.request);
    REQUIRE(outcome);
    CHECK(sameOutcome(*outcome, expected));
    CHECK(f.pad.bootloaderCommands == 0);
    CHECK(f.pad.holds == 0);
    CHECK_FALSE(f.recordLeft());
}

TEST_CASE("nfc flash: no writable directory, no flash", "[nfc_flash]")
{
    Fixture f;
    const auto blocker = f.dir.path / "file";
    std::ofstream(blocker) << "x";
    RecordStore unwritable {{blocker / "state"}};
    Flasher flasher {f.host, f.pad, unwritable};

    const auto outcome = flasher.run(f.request);
    REQUIRE(outcome);
    CHECK(sameOutcome(*outcome, {Status::Failed, Stage::Staging, Failure::Internal}));
    CHECK(f.pad.bootloaderCommands == 0);
}

TEST_CASE("nfc flash: the pad does not reach BOOTSEL", "[nfc_flash]")
{
    Fixture f;
    f.pad.bootloaderWorks = false;
    Outcome expected;
    bool kept = false;
    SECTION("it stays on its firmware")
    {
        expected = {Status::Failed, Stage::Bootloader, Failure::BootloaderRefused};
    }
    SECTION("it disappears, and may yet show up in BOOTSEL")
    {
        f.pad.vanishOnBootloader = true;
        expected = {Status::Failed, Stage::Bootloader, Failure::ProbeLost};
        kept = true;
    }
    const auto outcome = f.flasher().run(f.request);
    REQUIRE(outcome);
    CHECK(sameOutcome(*outcome, expected));
    CHECK(f.recordLeft() == kept);
}

TEST_CASE("nfc flash: a slow BOOTSEL after the command is still flashed", "[nfc_flash]")
{
    Fixture f;
    // Gone from its firmware at once, in BOOTSEL only after 15 s
    f.pad.vanishOnBootloader = true;
    f.pad.bootloaderWorks = false;
    f.pad.bootselAfterPolls = 60;
    const auto outcome = f.flasher().run(f.request);
    REQUIRE(outcome);
    CHECK(sameOutcome(*outcome, {Status::Succeeded, Stage::Done}));
}

TEST_CASE("nfc flash: resume rides out a slow pad", "[nfc_flash]")
{
    Fixture f;
    const auto stateDir = f.dir.path / "state";
    fs::create_directories(stateDir);
    REQUIRE(f.store.commit(stateDir, Record {"run-1", PORT, NEW_BUILD, f.request.sha256}));
    f.pad.firmware = NEW_BUILD;
    SECTION("it enumerates after 15 s")
    {
        f.pad.present = false;
        f.pad.appearAfterPolls = 60;
    }
    SECTION("its first build reads fail")
    {
        f.pad.failReads = 5;
    }
    std::vector<std::string> claims;
    const auto resumed = f.flasher().resume([&](const std::string &id) {
        claims.push_back(id);
        return true;
    });
    REQUIRE(resumed);
    CHECK(sameOutcome(resumed->outcome, {Status::Succeeded, Stage::Done}));
    CHECK(claims == std::vector<std::string> {"run-1"});
    CHECK_FALSE(f.recordLeft());
}

TEST_CASE("nfc flash: a copy that never verifies leaves the pad in BOOTSEL", "[nfc_flash]")
{
    Fixture f;
    f.pad.corruptReadAt = nfc::uf2::kFlashStart + 100;
    const auto outcome = f.flasher().run(f.request);

    REQUIRE(outcome);
    CHECK(sameOutcome(*outcome, {Status::Failed, Stage::Copy, Failure::StuckInBootloader}));
    CHECK(f.pad.opens == 3);
    CHECK_FALSE(f.pad.rom->rebooted);
    CHECK(f.recordLeft()); // the next start retries it

    // Meanwhile another run must not stage over that record.
    f.request.runId = "run-1"; // FakeHost checks the run id it reports
    const auto again = f.flasher().run(f.request);
    REQUIRE(again);
    CHECK(sameOutcome(*again, {Status::Failed, Stage::IdleCheck, Failure::Busy}));
    CHECK(f.recordLeft());

    // The next start, with the ROM reading back correctly again, finishes the pad.
    f.pad.rom->corruptReadAt = 0;
    const auto resumed = f.flasher().resume();
    REQUIRE(resumed);
    CHECK(sameOutcome(resumed->outcome, {Status::Succeeded, Stage::Done}));
    CHECK_FALSE(f.recordLeft());
}

TEST_CASE("nfc flash: what comes back after the reboot", "[nfc_flash]")
{
    Fixture f;
    Outcome expected;
    SECTION("an older build")
    {
        f.pad.rebootBuild = OLD_BUILD;
        expected = {Status::Failed, Stage::Verify, Failure::OldBuild};
    }
    SECTION("nothing")
    {
        f.pad.comesBack = false;
        expected = {Status::Failed, Stage::Reboot, Failure::NotResponding};
    }
    SECTION("a pad that enumerates but does not answer")
    {
        f.pad.failReadAfterReboot = true;
        expected = {Status::Failed, Stage::Verify, Failure::NotResponding};
    }
    const auto outcome = f.flasher().run(f.request);
    REQUIRE(outcome);
    CHECK(sameOutcome(*outcome, expected));
    CHECK_FALSE(f.recordLeft());
}

TEST_CASE("nfc flash: a stop in BOOTSEL keeps the record, and resume finishes", "[nfc_flash]")
{
    Fixture f;
    f.host.stopAt = Stage::Copy;
    CHECK_FALSE(f.flasher().run(f.request));
    CHECK(f.recordLeft());
    CHECK(f.pad.holds == 0);

    // After the restart
    f.host.stop = false;
    f.host.stopAt.reset();
    f.host.stages.clear();
    const auto resumed = f.flasher().resume();
    REQUIRE(resumed);
    CHECK(resumed->runId == "run-1");
    CHECK(sameOutcome(resumed->outcome, {Status::Succeeded, Stage::Done}));
    CHECK(f.host.stages == std::vector {Stage::Copy, Stage::Reboot, Stage::Verify});
    CHECK_FALSE(f.recordLeft());
    CHECK_FALSE(f.flasher().resume());
}

TEST_CASE("nfc flash: resume reports what a restart left behind", "[nfc_flash]")
{
    Fixture f;
    const auto stateDir = f.dir.path / "state";
    fs::create_directories(stateDir);
    REQUIRE(f.store.commit(stateDir, Record {"run-1", PORT, NEW_BUILD, f.request.sha256}));

    Outcome expected;
    SECTION("the pad is back on the target")
    {
        f.pad.firmware = NEW_BUILD;
        expected = {Status::Succeeded, Stage::Done};
    }
    SECTION("the pad never left its old build")
    {
        // Whether the command went out cannot be told, so the stage before it is reported.
        expected = {Status::Failed, Stage::Locking, Failure::Interrupted};
    }
    SECTION("the pad enumerates late, on the target")
    {
        f.pad.present = false;
        f.pad.appearAfterPolls = 4;
        f.pad.firmware = NEW_BUILD;
        expected = {Status::Succeeded, Stage::Done};
    }
    SECTION("the pad is gone")
    {
        f.pad.present = false;
        expected = {Status::Failed, Stage::Bootloader, Failure::ProbeLost};
    }
    SECTION("in BOOTSEL but the staged image is gone")
    {
        f.pad.present = false;
        f.pad.rom = std::make_shared<FakeBootrom>();
        expected = {Status::Failed, Stage::Copy, Failure::StuckInBootloader};
    }
    const auto resumed = f.flasher().resume();
    REQUIRE(resumed);
    CHECK(resumed->runId == "run-1");
    CHECK(sameOutcome(resumed->outcome, expected));
    CHECK_FALSE(f.recordLeft());
}

TEST_CASE("nfc flash: another process's run in the same directory is left alone", "[nfc_flash]")
{
    Fixture f;
    const auto stateDir = f.dir.path / "state";
    // Another SDK process of this user commits its run while this one downloads.
    f.host.duringDownload = [&] {
        fs::create_directories(stateDir);
        std::ofstream(RecordStore::imagePath(stateDir)) << "other image";
        REQUIRE(f.store.commit(stateDir, Record {"run-2", PORT, NEW_BUILD, "other"}));
    };

    const auto outcome = f.flasher().run(f.request);

    REQUIRE(outcome);
    CHECK(sameOutcome(*outcome, {Status::Failed, Stage::Locking, Failure::Busy}));
    CHECK(f.pad.bootloaderCommands == 0);
    const auto left = f.store.load();
    REQUIRE(left);
    CHECK(left->second.runId == "run-2");
    std::ifstream image(RecordStore::imagePath(stateDir));
    std::string text;
    std::getline(image, text);
    CHECK(text == "other image");
    for (const auto &entry : fs::directory_iterator(stateDir)) {
        CHECK(entry.path().extension() != ".part"); // this run's download is gone
    }
}

TEST_CASE("nfc flash: each run in a process stages under its own name", "[nfc_flash]")
{
    Fixture f;
    f.host.failDownload = true;
    f.flasher().run(f.request);
    f.flasher().run(f.request);
    REQUIRE(f.host.downloadedTo.size() == 2);
    CHECK(f.host.downloadedTo[0] != f.host.downloadedTo[1]);
}

TEST_CASE("nfc flash: resume leaves a run another process finished", "[nfc_flash]")
{
    Fixture f;
    const auto stateDir = f.dir.path / "state";
    fs::create_directories(stateDir);
    REQUIRE(f.store.commit(stateDir, Record {"run-1", PORT, NEW_BUILD, f.request.sha256}));
    f.pad.present = false;
    f.pad.rom = std::make_shared<FakeBootrom>();
    // The other process finishes and clears the run while this one waits for the lock.
    f.pad.beforeHold = [&] { f.store.clear(stateDir); };
    bool claimed = false; // so the caller sets no one-flash hold for it
    CHECK_FALSE(f.flasher().resume([&](const std::string &) { return claimed = true; }));
    CHECK_FALSE(claimed);
    CHECK(f.pad.opens == 0);
}

TEST_CASE("nfc flash: resume on a pad back on firmware clears only under the lock", "[nfc_flash]")
{
    Fixture f;
    const auto stateDir = f.dir.path / "state";
    fs::create_directories(stateDir);
    REQUIRE(f.store.commit(stateDir, Record {"run-1", PORT, NEW_BUILD, f.request.sha256}));
    SECTION("the lock is taken")
    {
        f.pad.lockBusy = true;
        CHECK_FALSE(f.flasher().resume());
        CHECK(f.store.load()->second.runId == "run-1");
    }
    SECTION("no lock that excludes other processes")
    {
        f.pad.noLock = true;
        CHECK_FALSE(f.flasher().resume());
        CHECK(f.store.load()->second.runId == "run-1");
    }
    SECTION("another process replaced the run meanwhile")
    {
        f.pad.beforeHold = [&] {
            REQUIRE(f.store.commit(stateDir, Record {"run-2", PORT, NEW_BUILD, "other"}));
        };
        CHECK_FALSE(f.flasher().resume());
        const auto left = f.store.load();
        REQUIRE(left);
        CHECK(left->second.runId == "run-2");
    }
}

TEST_CASE("nfc flash: resume yields to a flash that owns the unit", "[nfc_flash]")
{
    Fixture f;
    const auto stateDir = f.dir.path / "state";
    fs::create_directories(stateDir);
    REQUIRE(f.store.commit(stateDir, Record {"run-1", PORT, NEW_BUILD, f.request.sha256}));
    f.pad.present = false;
    f.pad.rom = std::make_shared<FakeBootrom>();
    CHECK_FALSE(f.flasher().resume([](const std::string &) { return false; }));
    CHECK(f.pad.opens == 0);
    CHECK(f.recordLeft());
}

TEST_CASE("nfc flash: a record that cannot be finished yet is kept", "[nfc_flash]")
{
    Fixture f;
    SECTION("resume finds the tap pad lock taken")
    {
        const auto stateDir = f.dir.path / "state";
        fs::create_directories(stateDir);
        REQUIRE(f.store.commit(stateDir, Record {"run-1", PORT, NEW_BUILD, f.request.sha256}));
        f.pad.present = false;
        f.pad.rom = std::make_shared<FakeBootrom>();
        f.pad.lockBusy = true;
        CHECK_FALSE(f.flasher().resume());
    }
    SECTION("a stop during verify")
    {
        f.pad.failReadAfterReboot = true;
        f.host.stopAt = Stage::Verify;
        CHECK_FALSE(f.flasher().run(f.request));
    }
    CHECK(f.recordLeft());
}

TEST_CASE("nfc flash: a deadline already past is deferred, idle or not", "[nfc_flash]")
{
    Fixture f;
    SECTION("before staging")
    {
        f.request.deadline = f.host.clock - 1s;
    }
    SECTION("crossed during the download")
    {
        f.request.deadline = f.host.clock + 1h;
        f.host.duringDownload = [&] { f.host.clock += 2h; };
    }
    const auto outcome = f.flasher().run(f.request);
    REQUIRE(outcome);
    CHECK(sameOutcome(*outcome, {Status::Deferred, Stage::IdleCheck}));
    CHECK(f.pad.bootloaderCommands == 0);
    CHECK_FALSE(f.recordLeft());
}

TEST_CASE("nfc flash: a stop while resume waits for the pad keeps the record", "[nfc_flash]")
{
    Fixture f;
    const auto stateDir = f.dir.path / "state";
    fs::create_directories(stateDir);
    REQUIRE(f.store.commit(stateDir, Record {"run-1", PORT, NEW_BUILD, f.request.sha256}));
    f.pad.present = false;
    f.host.stop = true;
    CHECK_FALSE(f.flasher().resume());
    CHECK(f.recordLeft());
}

#if !defined(_WIN32)
TEST_CASE("nfc flash: a record others could have written is not trusted", "[nfc_flash]")
{
    Fixture f;
    const auto stateDir = f.dir.path / "state";
    fs::create_directories(stateDir);
    REQUIRE(f.store.commit(stateDir, Record {"run-1", PORT, NEW_BUILD, f.request.sha256}));

    SECTION("a directory others can write to")
    {
        fs::permissions(stateDir, fs::perms::others_write, fs::perm_options::add);
        CHECK_FALSE(f.store.load());
        CHECK_FALSE(f.flasher().resume());
        CHECK(fs::exists(RecordStore::recordPath(stateDir))); // not this user's to remove
    }
    SECTION("a directory that is a link")
    {
        const auto linked = f.dir.path / "linked";
        fs::create_directory_symlink(stateDir, linked);
        RecordStore store {{linked}};
        CHECK_FALSE(store.load());
    }
    SECTION("a staged image that is a link is not written")
    {
        const auto real = f.dir.path / "real.uf2";
        std::ofstream(real, std::ios::binary)
                .write(reinterpret_cast<const char *>(f.host.image.data()),
                       std::streamsize(f.host.image.size()));
        fs::create_symlink(real, RecordStore::imagePath(stateDir));
        f.pad.present = false;
        f.pad.rom = std::make_shared<FakeBootrom>();
        const auto resumed = f.flasher().resume();
        REQUIRE(resumed);
        CHECK(sameOutcome(resumed->outcome,
                          {Status::Failed, Stage::Copy, Failure::StuckInBootloader}));
        CHECK(f.pad.opens == 0);
    }
}
#endif

TEST_CASE("nfc flash: the record store", "[nfc_flash]")
{
    TempDir dir;
    const auto blocked = dir.path / "file";
    std::ofstream(blocked) << "x";
    RecordStore store {{blocked / "a", dir.path / "b"}};

    const auto picked = store.pick();
    REQUIRE(picked);
    CHECK(*picked == dir.path / "b");
    CHECK(fs::is_empty(*picked));

    REQUIRE(store.commit(*picked, Record {"run-9", "1-1.3", NEW_BUILD, "ab"}));
    const auto loaded = store.load();
    REQUIRE(loaded);
    CHECK(loaded->first == *picked);
    CHECK(loaded->second.runId == "run-9");
    CHECK(loaded->second.port == "1-1.3");
    CHECK(loaded->second.targetBuild == NEW_BUILD);
    CHECK(loaded->second.sha256 == "ab");

    // Unreadable: ignored, left for the next commit, which runs under the lock, to replace.
    std::ofstream(RecordStore::recordPath(*picked)) << "{not json";
    CHECK_FALSE(store.load());
    CHECK(fs::exists(RecordStore::recordPath(*picked)));
    REQUIRE(store.commit(*picked, Record {"run-10", "1-1.3", NEW_BUILD, "ab"}));
    CHECK(store.load()->second.runId == "run-10");

    // Nothing but run-private files is ever written before the lock.
    for (const auto &entry : fs::directory_iterator(*picked)) {
        CHECK(entry.path().filename().string().rfind(".write-probe", 0) != 0);
    }

    CHECK_FALSE(RecordStore::defaultDirs().empty());
}

// SB-6130: a result outlives the record until the api settles it, so a restart resends it.
TEST_CASE("nfc flash: undelivered results are kept until dropped", "[nfc_flash]")
{
    TempDir dir;
    RecordStore store {{dir.path / "state"}};
    const std::string run = "38ff3bd2-549b-419d-9a31-8e624c352ac7";
    const std::string other = "4f6f766b-a81c-489a-b127-3d7a0dd2c888";
    const auto body = resultJson({Status::Succeeded, Stage::Done}).dump();

    REQUIRE(store.keepResult(run, body));
    REQUIRE(store.keepResult(other, body));
    auto kept = store.pendingResults();
    REQUIRE(kept.size() == 2);
    for (const auto &result : kept) {
        CHECK((result.runId == run || result.runId == other));
        CHECK(result.body == body);
    }

    // Kept beside a record, and untouched when the record goes.
    REQUIRE(store.commit(dir.path / "state", Record {run, "1-1.3", NEW_BUILD, "ab"}));
    store.clear(dir.path / "state");
    CHECK(store.pendingResults().size() == 2);

    store.dropResult(run);
    kept = store.pendingResults();
    REQUIRE(kept.size() == 1);
    CHECK(kept.front().runId == other);

    SECTION("a run id that is no uuid never becomes a file name")
    {
        CHECK_FALSE(store.keepResult("../escape", body));
        CHECK_FALSE(fs::exists(dir.path / "escape.json"));
    }
    SECTION("a result older than the cap is removed, not resent")
    {
        const auto file = RecordStore::resultPath(dir.path / "state", other);
        fs::last_write_time(file, fs::file_time_type::clock::now() - RESULT_MAX_AGE - 1h);
        CHECK(store.pendingResults().empty());
        CHECK_FALSE(fs::exists(file));
    }
    SECTION("an unreadable result is removed")
    {
        const auto file = RecordStore::resultPath(dir.path / "state", other);
        std::ofstream(file) << "{not json";
        CHECK(store.pendingResults().empty());
        CHECK_FALSE(fs::exists(file));
    }
}

TEST_CASE("nfc flash: sha256 and the result body", "[nfc_flash]")
{
    TempDir dir;
    std::ofstream(dir.path / "abc") << "abc";
    CHECK(sha256File(dir.path / "abc")
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256File(dir.path / "missing").empty());

    CHECK(resultJson({Status::Succeeded, Stage::Done})
          == nlohmann::json {{"status", "succeeded"}, {"stage", "done"}});
    CHECK(resultJson({Status::Failed, Stage::Copy, Failure::StuckInBootloader})
          == nlohmann::json {
                  {"status", "failed"}, {"stage", "copy"}, {"failure", "stuck_in_bootloader"}});
    CHECK(resultJson({Status::Deferred, Stage::IdleCheck})
          == nlohmann::json {{"status", "deferred"}, {"stage", "idle_check"}});
}
