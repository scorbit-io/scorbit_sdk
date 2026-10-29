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

// The SDK's own NFC tap pad flash (SB-5759): a probe_flash run for "nfc" is carried out here on
// every host, from the idle gate to the result, and a run cut short by a restart is resumed.
// The pad is written over PICOBOOT (SB-6046), never by mounting its RPI-RP2 drive.

#include <nfc/Picoboot.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace scorbit {
namespace detail {
namespace nfc_flash {

enum class Stage { IdleCheck, Staging, Locking, Bootloader, Copy, Reboot, Verify, Done };
enum class Status { Succeeded, Failed, Deferred };
enum class Failure {
    None,
    Download,
    Checksum,
    Busy,
    BootloaderRefused,
    StuckInBootloader,
    OldBuild,
    ProbeLost,
    NotResponding,
    ForeignBootloader,
    Interrupted,
    InvalidPolicy,
    Internal,
};

const char *stageName(Stage stage);
const char *statusName(Status status);
const char *failureName(Failure failure);

struct Outcome {
    Status status {Status::Failed};
    Stage stage {Stage::IdleCheck};
    Failure failure {Failure::None};
};

/** The probe_flash result body: status, stage and, unless it succeeded, the failure. */
nlohmann::json resultJson(const Outcome &outcome);

struct Request {
    std::string runId;
    uint64_t targetBuild {0};
    std::string sha256; // lowercase hex
    uint64_t size {0};
    std::optional<std::chrono::system_clock::time_point> deadline;
    std::string imageUrl;
};

/** What the routine needs from the SDK around it. */
class Host
{
public:
    virtual ~Host() = default;

    virtual bool stopping() = 0;
    /** No game in progress, and none for long enough that a flash will not meet one. */
    virtual bool idle() = 0;
    /** Waits @p duration; false, possibly early, once the SDK is stopping. */
    virtual bool sleepFor(std::chrono::milliseconds duration) = 0;
    virtual std::chrono::system_clock::time_point now() = 0;
    /** Reports progress; best effort. */
    virtual void stage(const std::string &runId, Stage stage) = 0;
    /** Downloads at most @p maxBytes from @p url into @p to. */
    virtual bool download(const std::string &url, const std::filesystem::path &to,
                          uint64_t maxBytes) = 0;
};

/** The tap pad and the USB devices around it. */
class Pad
{
public:
    /** Exclusive use of the pad for a flash; released on destruction. */
    class Hold
    {
    public:
        virtual ~Hold() = default;
        /** Port path of the NFC probe, if one is held and its port can be told. */
        virtual std::optional<std::string> port() = 0;
        /** Sends the probe to BOOTSEL and lets go of it; only the tap pad lock stays held. */
        virtual void enterBootloader() = 0;
    };

    /** Why @ref hold returned null. */
    enum class Refused {
        Busy,    // the lock, or the probe, is held elsewhere
        NoProbe, // no NFC probe is present
        NoLock   // no lock that excludes other processes can be had
    };

    virtual ~Pad() = default;

    /** Rescans for the NFC probe and reads its firmware build; nullopt if none answers. */
    virtual std::optional<uint64_t> build() = 0;
    /**
     * Takes the cross-process tap pad lock and, with @p withProbe, the SDK's own hold on the
     * NFC probe. Null, with @p why set, if either cannot be had.
     */
    virtual std::unique_ptr<Hold> hold(bool withProbe, Refused &why) = 0;
    /** Ports of every BOOTSEL device (2e8a:0003) on the bus. */
    virtual std::vector<std::string> bootselPorts() = 0;
    /** True if a Scorbit probe (USB vendor cafe) is at @p port. */
    virtual bool probeAt(const std::string &port) = 0;
    /** The PICOBOOT interface of the BOOTSEL device at @p port; null if it cannot be opened. */
    virtual std::unique_ptr<nfc::picoboot::Transport> openBootsel(const std::string &port) = 0;
};

/** The in-flight record a restart resumes from, kept with the downloaded image. */
struct Record {
    std::string runId;
    std::string port;
    uint64_t targetBuild {0};
    std::string sha256;
};

class RecordStore
{
public:
    /** @p dirs in order of preference; the first that can be written is used. */
    explicit RecordStore(std::vector<std::filesystem::path> dirs);

    /**
     * $XDG_STATE_HOME/scorbit (default ~/.local/state/scorbit) of the process's own user, then
     * scorbit-<uid> in the temp directory.
     */
    static std::vector<std::filesystem::path> defaultDirs();

    /** The first directory that can be created and written to, probed now. */
    std::optional<std::filesystem::path> pick() const;
    /** Writes, fsyncs and renames the record into place; false if any step fails. */
    bool commit(const std::filesystem::path &dir, const Record &record) const;
    /** The record left in any of the directories, with its directory. */
    std::optional<std::pair<std::filesystem::path, Record>> load() const;
    /** Removes the record and the image from @p dir. */
    void clear(const std::filesystem::path &dir) const;

    static std::filesystem::path imagePath(const std::filesystem::path &dir);
    static std::filesystem::path recordPath(const std::filesystem::path &dir);

private:
    std::vector<std::filesystem::path> m_dirs;
};

/** Lowercase hex sha256 of a file; empty if it cannot be read. */
std::string sha256File(const std::filesystem::path &file);

struct Timing {
    std::chrono::milliseconds idlePoll {std::chrono::seconds {5}};
    std::chrono::milliseconds devicePoll {std::chrono::milliseconds {250}};
    std::chrono::milliseconds bootselWait {std::chrono::seconds {10}};
    // How long a pad may take to show up after a mode change or a cold boot.
    std::chrono::milliseconds enumerateWait {std::chrono::seconds {60}};
    std::chrono::milliseconds rebootWait {std::chrono::seconds {30}};
    std::chrono::milliseconds verifyWait {std::chrono::seconds {15}};
    int copyAttempts {3};
};

class Flasher
{
public:
    Flasher(Host &host, Pad &pad, const RecordStore &store, Timing timing = {});

    /**
     * Carries out @p request. Nullopt only if the SDK stops once the pad is in BOOTSEL: the record
     * is kept, and @ref resume finishes the run after the restart.
     */
    std::optional<Outcome> run(const Request &request);

    struct Resumed {
        std::string runId;
        Outcome outcome;
    };

    /**
     * Finishes a run a restart cut short, if its record is left. @p claimed is called with its
     * run id once the run is this process's: under the tap pad lock, its record re-checked. If it
     * returns false the unit is busy with another flash: nothing is done and the record is kept.
     */
    std::optional<Resumed> resume(const std::function<bool(const std::string &)> &claimed = {});

private:
    std::optional<Outcome> waitForIdle(const Request &request);
    std::optional<Outcome> finish(const std::filesystem::path &dir, const Record &record);
    bool waitFor(std::chrono::milliseconds limit, const std::function<bool()> &done);
    bool inBootsel(const std::string &port);

    Host &m_host;
    Pad &m_pad;
    const RecordStore &m_store;
    Timing m_timing;
};

} // namespace nfc_flash
} // namespace detail
} // namespace scorbit
