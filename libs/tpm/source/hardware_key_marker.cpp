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

#include "tpm/hardware_key_marker.h"

#include <logger/logger.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#    include <io.h>
#    include <process.h>
#else
#    include <cerrno>
#    include <fcntl.h>
#    include <pwd.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

// The marker file's own format; not an api payload.
constexpr auto KEY_SERIAL = "serial";
constexpr auto KEY_UUID = "uuid";

constexpr std::size_t MAX_MARKER_SIZE = 4096;

std::string uniqueSuffix()
{
    static std::atomic<unsigned> writes {0};
#if defined(_WIN32)
    const long pid = long(::_getpid());
#else
    const long pid = long(::getpid());
#endif
    return "." + std::to_string(pid) + "." + std::to_string(++writes) + ".tmp";
}

#if !defined(_WIN32)
bool trustedOwner(uid_t uid)
{
    return uid == ::geteuid() || uid == 0;
}
#endif

#if !defined(_WIN32)
// Every directory above @p dir, links resolved: one others could write to (unless sticky) would
// let them rename @p dir, marker and all, out of the way.
bool hasTrustedAncestors(const fs::path &dir)
{
    std::error_code ec;
    const auto parent = fs::canonical(fs::absolute(dir, ec).parent_path(), ec);
    if (ec) {
        return false;
    }
    for (auto p = parent;; p = p.parent_path()) {
        struct stat st {
        };
        if (::lstat(p.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || !trustedOwner(st.st_uid)
            || ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0 && (st.st_mode & S_ISVTX) == 0)) {
            return false;
        }
        if (p == p.root_path()) {
            return true;
        }
    }
}
#endif

// Only a directory nobody but its owner (this user or root) can change may hold the marker: in a
// world-writable one, such as a 777 library directory, anyone could plant or delete it.
bool isTrustedDirectory([[maybe_unused]] const fs::path &dir)
{
#if defined(_WIN32)
    std::error_code ec;
    return fs::is_directory(fs::symlink_status(dir, ec));
#else
    struct stat st {
    };
    return ::lstat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && trustedOwner(st.st_uid)
        && (st.st_mode & (S_IWGRP | S_IWOTH)) == 0 && hasTrustedAncestors(dir);
#endif
}

// A directory of ours others can write to, e.g. created under a loose umask, is made private.
void tightenOwnDirectory([[maybe_unused]] const fs::path &dir)
{
#if !defined(_WIN32)
    struct stat st {
    };
    if (::lstat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == ::geteuid()
        && (st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        std::error_code ec;
        fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
    }
#endif
}

// The staged file is created exclusively and never through a link planted under its name.
bool writeDurably(const fs::path &file, const std::string &content)
{
#if defined(_WIN32)
    std::FILE *out = std::fopen(file.string().c_str(), "wb");
    if (!out) {
        return false;
    }
    bool ok = std::fwrite(content.data(), 1, content.size(), out) == content.size()
           && std::fflush(out) == 0 && ::_commit(::_fileno(out)) == 0;
    return std::fclose(out) == 0 && ok;
#else
    const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }
    bool ok = ::write(fd, content.data(), content.size()) == ssize_t(content.size())
           && ::fsync(fd) == 0;
    return ::close(fd) == 0 && ok;
#endif
}

bool syncDirectory([[maybe_unused]] const fs::path &dir)
{
#if defined(_WIN32)
    return true;
#else
    const int fd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool ok = ::fsync(fd) == 0;
    return ::close(fd) == 0 && ok;
#endif
}

std::optional<fs::path> homeDirectory()
{
    if (const char *home = std::getenv("HOME"); home && *home) {
        return fs::path(home);
    }
#if !defined(_WIN32)
    // A service started with no HOME, e.g. as root from init.
    if (const passwd *pw = ::getpwuid(::geteuid()); pw && pw->pw_dir && *pw->pw_dir) {
        return fs::path(pw->pw_dir);
    }
#endif
    return std::nullopt;
}

// At most MAX_MARKER_SIZE bytes of a regular file this user or root owns; nullopt otherwise.
std::optional<std::string> readTrustedFile(const fs::path &file)
{
#if defined(_WIN32)
    std::error_code ec;
    if (!fs::is_regular_file(fs::symlink_status(file, ec))) {
        return std::nullopt;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }
    std::string body(MAX_MARKER_SIZE + 1, '\0');
    in.read(body.data(), std::streamsize(body.size()));
    body.resize(std::size_t(in.gcount()));
#else
    int fd = -1;
    do {
        // O_NONBLOCK: a FIFO planted under the name must not block until a writer turns up.
        fd = ::open(file.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        return std::nullopt;
    }
    struct stat st {
    };
    std::string body;
    if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && trustedOwner(st.st_uid)) {
        body.resize(MAX_MARKER_SIZE + 1);
        std::size_t got = 0;
        ssize_t n = 0;
        while (got < body.size()) {
            n = ::read(fd, body.data() + got, body.size() - got);
            if (n < 0 && errno == EINTR) {
                continue;
            }
            if (n <= 0) {
                break;
            }
            got += std::size_t(n);
        }
        body.resize(n < 0 ? 0 : got);
    }
    ::close(fd);
    if (body.empty()) {
        return std::nullopt;
    }
#endif
    if (body.size() > MAX_MARKER_SIZE) {
        return std::nullopt;
    }
    return body;
}

std::optional<HardwareKeyIdentity> readMarker(const fs::path &dir)
{
    const auto file = dir / HardwareKeyMarker::FILE_NAME;
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(file, ec))) {
        return std::nullopt;
    }
    if (!isTrustedDirectory(dir)) {
        WRN("Hardware-key marker {} is in a directory others can change; ignoring it",
            file.string());
        return std::nullopt;
    }
    const auto body = readTrustedFile(file);
    if (!body) {
        WRN("Hardware-key marker {} is unreadable, not owned by this user or root, or too large; "
            "ignoring it",
            file.string());
        return std::nullopt;
    }
    try {
        const auto j = json::parse(*body);
        HardwareKeyIdentity identity {j.at(KEY_SERIAL).get<uint64_t>(),
                                      j.at(KEY_UUID).get<std::string>()};
        if (identity.uuid.empty()) {
            WRN("Hardware-key marker {} has no uuid, ignoring it", file.string());
            return std::nullopt;
        }
        return identity;
    } catch (const std::exception &e) {
        WRN("Hardware-key marker {} is malformed, ignoring it: {}", file.string(), e.what());
        return std::nullopt;
    }
}

} // namespace

HardwareKeyMarker::HardwareKeyMarker()
    : HardwareKeyMarker(defaultDirs())
{
}

HardwareKeyMarker::HardwareKeyMarker(std::vector<fs::path> dirs)
    : m_dirs {std::move(dirs)}
{
}

std::vector<fs::path> HardwareKeyMarker::defaultDirs()
{
    std::vector<fs::path> dirs;
    try {
        if (const char *state = std::getenv("XDG_STATE_HOME");
            state && *state && fs::path(state).is_absolute()) {
            dirs.push_back(fs::path(state) / "scorbit");
        } else if (auto home = homeDirectory()) {
            dirs.push_back(*home / ".local" / "state" / "scorbit");
        }
#if defined(_WIN32)
        if (const char *local = std::getenv("LOCALAPPDATA"); local && *local) {
            dirs.push_back(fs::path(local) / "scorbit");
        }
#endif
    } catch (const std::exception &e) {
        WRN("Hardware-key marker: can't work out its directories: {}", e.what());
    }
    return dirs;
}

bool HardwareKeyMarker::store(const HardwareKeyIdentity &identity) const
{
    try {
        const auto body = json {{KEY_SERIAL, identity.serial}, {KEY_UUID, identity.uuid}}.dump();
        for (const auto &dir : m_dirs) {
            std::error_code ec;
            if (fs::create_directories(dir, ec)) {
                fs::permissions(dir, fs::perms::owner_all, fs::perm_options::replace, ec);
            }
            tightenOwnDirectory(dir);
            if (!isTrustedDirectory(dir)) {
                continue;
            }
            const auto target = dir / FILE_NAME;
            auto staged = target;
            staged += uniqueSuffix();
            if (!writeDurably(staged, body)) {
                fs::remove(staged, ec);
                continue;
            }
            fs::rename(staged, target, ec);
            if (ec) {
                fs::remove(staged, ec);
                continue;
            }
            // Until the directory is synced the rename may not survive a power loss.
            if (!syncDirectory(dir)) {
                WRN("Hardware-key marker: could not sync {}, trying the next directory",
                    dir.string());
                continue;
            }
            return true;
        }
    } catch (const std::exception &e) {
        ERR("Hardware-key marker: store failed: {}", e.what());
        return false;
    }
    // Temp is never tried: a marker lost at reboot would let a soft key take over after all.
    ERR("Hardware-key marker: no durable directory owned by this user or root can take it; an "
        "absent tap pad may fall back to a soft key");
    return false;
}

std::optional<HardwareKeyIdentity> HardwareKeyMarker::load() const
{
    for (const auto &dir : m_dirs) {
        try {
            if (auto identity = readMarker(dir)) {
                return identity;
            }
        } catch (const std::exception &e) {
            WRN("Hardware-key marker: can't read it: {}", e.what());
        }
    }
    return std::nullopt;
}

void HardwareKeyMarker::clear() const
{
    for (const auto &dir : m_dirs) {
        if (isTrustedDirectory(dir)) {
            std::error_code ec;
            fs::remove(dir / FILE_NAME, ec);
        }
    }
}

bool HardwareKeyMarker::record(const HardwareKeyIdentity &identity) const
{
    const auto existing = load();
    if (existing == identity) {
        return true;
    }
    if (existing) {
        INF("Hardware key changed (serial {} -> {}); updating the hardware-key marker",
            existing->serial, identity.serial);
    }
    if (!store(identity)) {
        return false;
    }
    // An older marker earlier in the list that this process could not replace still wins.
    if (load() != identity) {
        ERR("Hardware-key marker: an older marker this process cannot replace takes precedence");
        return false;
    }
    return true;
}
