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

#include "nfc_flash.h"

#include "identifiers.h"

#include <logger/logger.h>
#include <nfc/Uf2.h>

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#    include <io.h>
#else
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace scorbit {
namespace detail {
namespace nfc_flash {

namespace fs = std::filesystem;
using json = nlohmann::json;

const char *stageName(Stage stage)
{
    switch (stage) {
    case Stage::IdleCheck:
        return JVAL_PF_STAGE_IDLE_CHECK;
    case Stage::Staging:
        return JVAL_PF_STAGE_STAGING;
    case Stage::Locking:
        return JVAL_PF_STAGE_LOCKING;
    case Stage::Bootloader:
        return JVAL_PF_STAGE_BOOTLOADER;
    case Stage::Copy:
        return JVAL_PF_STAGE_COPY;
    case Stage::Reboot:
        return JVAL_PF_STAGE_REBOOT;
    case Stage::Verify:
        return JVAL_PF_STAGE_VERIFY;
    case Stage::Done:
        return JVAL_PF_STAGE_DONE;
    }
    return JVAL_PF_STAGE_IDLE_CHECK;
}

const char *statusName(Status status)
{
    switch (status) {
    case Status::Succeeded:
        return JVAL_PF_STATUS_SUCCEEDED;
    case Status::Failed:
        return JVAL_PF_STATUS_FAILED;
    case Status::Deferred:
        return JVAL_PF_STATUS_DEFERRED;
    }
    return JVAL_PF_STATUS_FAILED;
}

const char *failureName(Failure failure)
{
    switch (failure) {
    case Failure::None:
        return "none";
    case Failure::Download:
        return JVAL_PF_FAILURE_DOWNLOAD;
    case Failure::Checksum:
        return JVAL_PF_FAILURE_CHECKSUM;
    case Failure::Busy:
        return JVAL_PF_FAILURE_BUSY;
    case Failure::BootloaderRefused:
        return JVAL_PF_FAILURE_BOOTLOADER_REFUSED;
    case Failure::StuckInBootloader:
        return JVAL_PF_FAILURE_STUCK_IN_BOOTLOADER;
    case Failure::OldBuild:
        return JVAL_PF_FAILURE_OLD_BUILD;
    case Failure::ProbeLost:
        return JVAL_PF_FAILURE_PROBE_LOST;
    case Failure::NotResponding:
        return JVAL_PF_FAILURE_NOT_RESPONDING;
    case Failure::ForeignBootloader:
        return JVAL_PF_FAILURE_FOREIGN_BOOTLOADER;
    case Failure::Interrupted:
        return JVAL_PF_FAILURE_INTERRUPTED;
    case Failure::InvalidPolicy:
        return JVAL_PF_FAILURE_INVALID_POLICY;
    case Failure::Internal:
        return JVAL_PF_FAILURE_INTERNAL;
    }
    return JVAL_PF_FAILURE_INTERNAL;
}

json resultJson(const Outcome &outcome)
{
    json body {{JKEY_PF_STATUS, statusName(outcome.status)},
               {JKEY_PF_STAGE, stageName(outcome.stage)}};
    if (outcome.failure != Failure::None) {
        body[JKEY_PF_FAILURE] = failureName(outcome.failure);
    }
    return body;
}

// ---- Record store ----

namespace {

#if !defined(_WIN32)
// A directory this process may keep its record in: a real directory (not a link someone else
// planted, which matters in the shared temp directory) owned by this user.
bool isOwnDirectory(const fs::path &dir)
{
    struct stat st {
    };
    return ::lstat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == ::getuid();
}
#endif

bool writeDurably(const fs::path &file, const std::string &content)
{
    std::FILE *out = std::fopen(file.string().c_str(), "wb");
    if (!out) {
        return false;
    }
    bool ok = std::fwrite(content.data(), 1, content.size(), out) == content.size()
           && std::fflush(out) == 0;
#if defined(_WIN32)
    ok = ok && ::_commit(::_fileno(out)) == 0;
#else
    ok = ok && ::fsync(::fileno(out)) == 0;
#endif
    return std::fclose(out) == 0 && ok;
}

void syncDirectory([[maybe_unused]] const fs::path &dir)
{
#if !defined(_WIN32)
    // The rename is only durable once the directory entry is on disk.
    if (const int fd = ::open(dir.c_str(), O_RDONLY); fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#endif
}

} // namespace

RecordStore::RecordStore(std::vector<fs::path> dirs)
    : m_dirs {std::move(dirs)}
{
}

std::vector<fs::path> RecordStore::defaultDirs()
{
    std::vector<fs::path> dirs;
    if (const char *state = std::getenv("XDG_STATE_HOME");
        state && *state && fs::path(state).is_absolute()) {
        dirs.push_back(fs::path(state) / "scorbit");
    } else if (const char *home = std::getenv("HOME"); home && *home) {
        dirs.push_back(fs::path(home) / ".local" / "state" / "scorbit");
    }
#if defined(_WIN32)
    if (const char *local = std::getenv("LOCALAPPDATA"); local && *local) {
        dirs.push_back(fs::path(local) / "scorbit");
    }
#endif
    std::error_code ec;
    if (const auto tmp = fs::temp_directory_path(ec); !ec) {
#if defined(_WIN32)
        dirs.push_back(tmp / "scorbit");
#else
        dirs.push_back(tmp / ("scorbit-" + std::to_string(::getuid())));
#endif
    }
    return dirs;
}

std::optional<fs::path> RecordStore::pick() const
{
    for (const auto &dir : m_dirs) {
        std::error_code ec;
        fs::create_directories(dir, ec);
#if !defined(_WIN32)
        if (!isOwnDirectory(dir)) {
            continue;
        }
        fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
#endif
        const auto probe = dir / ".write-probe";
        if (writeDurably(probe, "ok")) {
            fs::remove(probe, ec);
            return dir;
        }
        fs::remove(probe, ec);
    }
    return std::nullopt;
}

bool RecordStore::commit(const fs::path &dir, const Record &record) const
{
    const json body {{JKEY_DIAG_RUN_ID, record.runId},
                     {JKEY_NFR_PORT, record.port},
                     {JKEY_PF_TARGET_BUILD, record.targetBuild},
                     {JKEY_PF_SHA256, record.sha256}};
    auto staged = recordPath(dir);
    staged += ".tmp";
    if (!writeDurably(staged, body.dump())) {
        std::error_code ec;
        fs::remove(staged, ec);
        return false;
    }
    std::error_code ec;
    fs::rename(staged, recordPath(dir), ec);
    if (ec) {
        fs::remove(staged, ec);
        return false;
    }
    syncDirectory(dir);
    return true;
}

std::optional<std::pair<fs::path, Record>> RecordStore::load() const
{
    for (const auto &dir : m_dirs) {
        std::ifstream in(recordPath(dir));
        if (!in) {
            continue;
        }
        try {
            const auto body = json::parse(in);
            Record record;
            record.runId = body.at(JKEY_DIAG_RUN_ID).get<std::string>();
            record.port = body.at(JKEY_NFR_PORT).get<std::string>();
            record.targetBuild = body.at(JKEY_PF_TARGET_BUILD).get<uint64_t>();
            record.sha256 = body.at(JKEY_PF_SHA256).get<std::string>();
            return std::make_pair(dir, record);
        } catch (const std::exception &e) {
            WRN("NFC flash: unreadable record in {} ignored: {}", dir.string(), e.what());
            clear(dir);
        }
    }
    return std::nullopt;
}

void RecordStore::clear(const fs::path &dir) const
{
    std::error_code ec;
    fs::remove(recordPath(dir), ec);
    fs::remove(imagePath(dir), ec);
    auto part = imagePath(dir);
    part += ".part";
    fs::remove(part, ec);
}

fs::path RecordStore::imagePath(const fs::path &dir)
{
    return dir / "nfc-flash.uf2";
}

fs::path RecordStore::recordPath(const fs::path &dir)
{
    return dir / "nfc-flash.json";
}

std::string sha256File(const fs::path &file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return {};
    }
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx {EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
        return {};
    }
    std::array<char, 64 * 1024> buffer {};
    while (in) {
        in.read(buffer.data(), std::streamsize(buffer.size()));
        if (in.gcount() > 0
            && EVP_DigestUpdate(ctx.get(), buffer.data(), size_t(in.gcount())) != 1) {
            return {};
        }
    }
    if (in.bad()) {
        return {};
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest {};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest.data(), &length) != 1) {
        return {};
    }
    static constexpr char HEX[] = "0123456789abcdef";
    std::string hex;
    for (unsigned int i = 0; i < length; ++i) {
        hex += HEX[digest[i] >> 4];
        hex += HEX[digest[i] & 0x0F];
    }
    return hex;
}

// ---- Flasher ----

Flasher::Flasher(Host &host, Pad &pad, const RecordStore &store, Timing timing)
    : m_host {host}
    , m_pad {pad}
    , m_store {store}
    , m_timing {timing}
{
}

bool Flasher::waitFor(std::chrono::milliseconds limit, const std::function<bool()> &done)
{
    for (std::chrono::milliseconds waited {0};; waited += m_timing.devicePoll) {
        if (done()) {
            return true;
        }
        if (waited >= limit || !m_host.sleepFor(m_timing.devicePoll)) {
            return false;
        }
    }
}

bool Flasher::inBootsel(const std::string &port)
{
    const auto ports = m_pad.bootselPorts();
    return std::find(ports.begin(), ports.end(), port) != ports.end();
}

std::optional<Outcome> Flasher::waitForIdle(const Request &request)
{
    while (!m_host.idle()) {
        if (request.deadline && m_host.now() >= *request.deadline) {
            return Outcome {Status::Deferred, Stage::IdleCheck};
        }
        if (!m_host.sleepFor(m_timing.idlePoll)) {
            return Outcome {Status::Failed, Stage::IdleCheck, Failure::Interrupted};
        }
    }
    return std::nullopt;
}

std::optional<Outcome> Flasher::run(const Request &request)
{
    const auto &runId = request.runId;
    m_host.stage(runId, Stage::IdleCheck);
    if (auto deferred = waitForIdle(request)) {
        return deferred;
    }
    const auto current = m_pad.build();
    if (!current) {
        return Outcome {Status::Failed, Stage::IdleCheck, Failure::ProbeLost};
    }
    if (*current == request.targetBuild) {
        INF("NFC flash run_id={}: the pad is already on build {}", runId, *current);
        return Outcome {Status::Succeeded, Stage::Done};
    }

    m_host.stage(runId, Stage::Staging);
    const auto dir = m_store.pick();
    if (!dir) {
        ERR("NFC flash run_id={}: no writable directory for the image and record", runId);
        return Outcome {Status::Failed, Stage::Staging, Failure::Internal};
    }
    INF("NFC flash run_id={}: build {} -> {}, staging in {}", runId, *current, request.targetBuild,
        dir->string());
    m_store.clear(*dir);
    auto fail = [&](Stage stage, Failure failure) {
        m_store.clear(*dir);
        return Outcome {Status::Failed, stage, failure};
    };

    auto part = RecordStore::imagePath(*dir);
    part += ".part";
    if (!m_host.download(request.imageUrl, part, request.size)) {
        return fail(Stage::Staging, Failure::Download);
    }
    std::error_code ec;
    if (fs::file_size(part, ec) != request.size || ec || sha256File(part) != request.sha256) {
        return fail(Stage::Staging, Failure::Checksum);
    }
    if (const auto image = nfc::uf2::parseFile(part); !image.ok()) {
        WRN("NFC flash run_id={}: the image is not a tap pad UF2 ({})", runId,
            nfc::uf2::errorName(image.error));
        return fail(Stage::Staging, Failure::InvalidPolicy);
    }
    fs::rename(part, RecordStore::imagePath(*dir), ec);
    if (ec) {
        return fail(Stage::Staging, Failure::Internal);
    }

    m_host.stage(runId, Stage::Locking);
    if (auto deferred = waitForIdle(request)) {
        m_store.clear(*dir);
        return deferred;
    }
    bool busy = false;
    auto hold = m_pad.hold(true, busy);
    if (!hold) {
        return fail(Stage::Locking, busy ? Failure::Busy : Failure::ProbeLost);
    }
    const auto port = hold->port();
    if (!port) {
        ERR("NFC flash run_id={}: the pad's USB port cannot be told", runId);
        return fail(Stage::Locking, Failure::Internal);
    }
    // Only this pad may be in BOOTSEL once the command is sent; any other one is not ours.
    if (const auto foreign = m_pad.bootselPorts(); !foreign.empty()) {
        WRN("NFC flash run_id={}: a BOOTSEL device is already at {}", runId, foreign.front());
        return fail(Stage::Locking, Failure::ForeignBootloader);
    }
    const Record record {runId, *port, request.targetBuild, request.sha256};
    if (!m_store.commit(*dir, record)) {
        ERR("NFC flash run_id={}: the record could not be committed in {}", runId, dir->string());
        return fail(Stage::Locking, Failure::Internal);
    }
    if (m_host.stopping()) {
        return fail(Stage::Locking, Failure::Interrupted);
    }

    hold->enterBootloader();
    m_host.stage(runId, Stage::Bootloader);
    if (!waitFor(m_timing.bootselWait, [&] { return inBootsel(*port); })) {
        if (m_host.stopping()) {
            return std::nullopt; // resumed after the restart
        }
        return fail(Stage::Bootloader,
                    m_pad.probeAt(*port) ? Failure::BootloaderRefused : Failure::ProbeLost);
    }
    return finish(*dir, record);
}

std::optional<Outcome> Flasher::finish(const fs::path &dir, const Record &record)
{
    const auto &runId = record.runId;
    const auto &port = record.port;
    auto end = [&](Stage stage, Failure failure) {
        m_store.clear(dir);
        return Outcome {Status::Failed, stage, failure};
    };

    m_host.stage(runId, Stage::Copy);
    const auto imageFile = RecordStore::imagePath(dir);
    const auto image = nfc::uf2::parseFile(imageFile);
    if (!image.ok() || sha256File(imageFile) != record.sha256) {
        ERR("NFC flash run_id={}: the staged image is gone or changed", runId);
        return end(Stage::Copy, Failure::StuckInBootloader);
    }
    bool copied = false;
    for (int attempt = 1; attempt <= m_timing.copyAttempts && !copied; ++attempt) {
        if (m_host.stopping()) {
            return std::nullopt;
        }
        auto transport = m_pad.openBootsel(port);
        if (!transport) {
            WRN("NFC flash run_id={}: PICOBOOT at {} cannot be opened (attempt {})", runId, port,
                attempt);
            m_host.sleepFor(m_timing.devicePoll);
            continue;
        }
        const auto result = nfc::picoboot::flash(*transport, image.image);
        copied = result.ok();
        if (!copied) {
            WRN("NFC flash run_id={}: PICOBOOT {} failed at {} 0x{:08x} (attempt {})", runId,
                nfc::picoboot::errorName(result.error), nfc::picoboot::stepName(result.step),
                result.address, attempt);
        }
    }
    if (!copied) {
        return end(Stage::Copy, inBootsel(port) ? Failure::StuckInBootloader : Failure::ProbeLost);
    }

    m_host.stage(runId, Stage::Reboot);
    if (!waitFor(m_timing.rebootWait, [&] { return !inBootsel(port) && m_pad.probeAt(port); })) {
        if (m_host.stopping()) {
            return std::nullopt;
        }
        return end(Stage::Reboot,
                   inBootsel(port) ? Failure::StuckInBootloader : Failure::NotResponding);
    }

    m_host.stage(runId, Stage::Verify);
    std::optional<uint64_t> build;
    waitFor(m_timing.verifyWait, [&] { return (build = m_pad.build()).has_value(); });
    if (!build) {
        if (m_host.stopping()) {
            return std::nullopt; // the restart verifies it
        }
        return end(Stage::Verify, Failure::NotResponding);
    }
    if (*build != record.targetBuild) {
        WRN("NFC flash run_id={}: the pad came back on build {}, not {}", runId, *build,
            record.targetBuild);
        return end(Stage::Verify, Failure::OldBuild);
    }
    m_store.clear(dir);
    INF("NFC flash run_id={}: the pad is on build {}", runId, *build);
    return Outcome {Status::Succeeded, Stage::Done};
}

std::optional<Flasher::Resumed> Flasher::resume()
{
    const auto loaded = m_store.load();
    if (!loaded) {
        return std::nullopt;
    }
    const auto &[dir, record] = *loaded;
    INF("NFC flash run_id={}: resuming from {} (pad port {})", record.runId, dir.string(),
        record.port);

    // After a cold boot the pad, in BOOTSEL or not, may not have enumerated yet.
    const auto &port = record.port;
    waitFor(m_timing.bootselWait, [&] { return inBootsel(port) || m_pad.probeAt(port); });

    Outcome outcome;
    if (inBootsel(port) && !m_pad.probeAt(port)) {
        bool busy = false;
        std::unique_ptr<Pad::Hold> hold;
        waitFor(m_timing.bootselWait, [&] { return (hold = m_pad.hold(false, busy)) != nullptr; });
        if (!hold) {
            // Only this record can bring the pad out of BOOTSEL; keep it for the next start.
            WRN("NFC flash run_id={}: the tap pad lock is busy; resume left for the next start",
                record.runId);
            return std::nullopt;
        }
        auto finished = finish(dir, record);
        if (!finished) {
            return std::nullopt; // stopping again; the record is still there
        }
        outcome = *finished;
    } else if (m_pad.probeAt(port)) {
        // Cut short before the pad left its firmware, or after it came back. Whether the
        // command went out cannot be told, so an unflashed pad reports the stage before it.
        const auto build = m_pad.build();
        outcome = build == record.targetBuild
                        ? Outcome {Status::Succeeded, Stage::Done}
                        : Outcome {Status::Failed, Stage::Locking, Failure::Interrupted};
        m_store.clear(dir);
    } else {
        outcome = Outcome {Status::Failed, Stage::Bootloader, Failure::ProbeLost};
        m_store.clear(dir);
    }
    return Resumed {record.runId, outcome};
}

} // namespace nfc_flash
} // namespace detail
} // namespace scorbit
