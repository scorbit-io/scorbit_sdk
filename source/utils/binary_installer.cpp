/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include "binary_installer.h"
#include <logger/logger.h>

#include <boost/filesystem.hpp>
#include <boost/predef.h>
#include <fmt/format.h>
#include <algorithm>
#include <array>
#include <fstream>

#if BOOST_OS_WINDOWS
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#else
#    include <cerrno>
#    include <cstring>
#    include <fcntl.h>
#    include <unistd.h>
#endif

namespace {

namespace fs = boost::filesystem;

constexpr auto STAGED_SUFFIX = ".new";
constexpr auto BACKUP_SUFFIX = ".old";

using Magic = std::array<unsigned char, 4>;

// True if the leading bytes are those of an executable image of this platform's format
bool hasExecutableMagic(const Magic &header)
{
#if BOOST_OS_WINDOWS
    return header[0] == 'M' && header[1] == 'Z';
#elif BOOST_OS_MACOS
    // Mach-O, 32 or 64-bit in either byte order, or a universal binary
    static constexpr std::array<Magic, 7> MAGICS {{
            {0xfe, 0xed, 0xfa, 0xce},
            {0xce, 0xfa, 0xed, 0xfe},
            {0xfe, 0xed, 0xfa, 0xcf},
            {0xcf, 0xfa, 0xed, 0xfe},
            {0xca, 0xfe, 0xba, 0xbe},
            {0xbe, 0xba, 0xfe, 0xca},
            {0xca, 0xfe, 0xba, 0xbf},
    }};
    return std::find(MAGICS.begin(), MAGICS.end(), header) != MAGICS.end();
#else
    return header[0] == 0x7f && header[1] == 'E' && header[2] == 'L' && header[3] == 'F';
#endif
}

// Flushes a file's data, or a directory's entries, to the disk. Without it a power cut can leave a
// renamed file with its new name but none of its data, i.e. empty.
bool syncToDisk(const fs::path &path, std::string &error)
{
#if BOOST_OS_WINDOWS
    // Directories can't be flushed on Windows; the moves are made with MOVEFILE_WRITE_THROUGH
    boost::system::error_code ec;
    if (fs::is_directory(path, ec)) {
        return true;
    }

    const HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = fmt::format("Can't open {} to sync: error {}", path.string(), GetLastError());
        return false;
    }
    const bool ok = FlushFileBuffers(h) != FALSE;
    const auto lastError = GetLastError();
    CloseHandle(h);
    if (!ok) {
        error = fmt::format("Can't sync {}: error {}", path.string(), lastError);
        return false;
    }
    return true;
#else
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        error = fmt::format("Can't open {} to sync: {}", path.string(), std::strerror(errno));
        return false;
    }
    const int rc = ::fsync(fd);
    const int syncErrno = errno;
    ::close(fd);
    if (rc != 0) {
        error = fmt::format("Can't sync {}: {}", path.string(), std::strerror(syncErrno));
        return false;
    }
    return true;
#endif
}

// Renames @p from to @p to, replacing @p to if it exists
bool moveFile(const fs::path &from, const fs::path &to, std::string &error)
{
#if BOOST_OS_WINDOWS
    if (!MoveFileExW(from.c_str(), to.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = fmt::format("Can't move {} to {}: error {}", from.string(), to.string(),
                            GetLastError());
        return false;
    }
#else
    boost::system::error_code ec;
    fs::rename(from, to, ec); // atomic on POSIX: @p to is the old or the new file, never missing
    if (ec) {
        error = fmt::format("Can't move {} to {}: {}", from.string(), to.string(), ec.message());
        return false;
    }
#endif
    return true;
}

} // namespace

namespace scorbit {
namespace detail {

bool isExecutableImage(const fs::path &path, std::string &error)
{
    boost::system::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        error = fmt::format("Not a regular file: {}", path.string());
        return false;
    }

    const auto size = fs::file_size(path, ec);
    if (ec || size == 0) {
        error = fmt::format("Empty file: {}", path.string());
        return false;
    }

    Magic header {};
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in.read(reinterpret_cast<char *>(header.data()), header.size())) {
        error = fmt::format("Too short to be an executable: {}, {} bytes", path.string(), size);
        return false;
    }

    if (!hasExecutableMagic(header)) {
        error = fmt::format("Not an executable for this platform: {}", path.string());
        return false;
    }

    return true;
}

bool installBinary(const fs::path &target, const fs::path &candidate, std::string &error,
                   const InstalledCheck &check)
{
    const auto staged = fs::path {target} += STAGED_SUFFIX;
    const auto backup = fs::path {target} += BACKUP_SUFFIX;
    const auto dir = target.parent_path();
    boost::system::error_code ec;

    // Leftovers of an interrupted update. Neither is ever the only good copy: the target is
    // swapped only once the new binary is on the disk, and the backup is dropped only after that.
    fs::remove(staged, ec);
    fs::remove(backup, ec);

    if (!isExecutableImage(candidate, error)) {
        return false;
    }
    const auto size = fs::file_size(candidate, ec);

    // 1. Stage the new binary next to the target, with the target's permissions, and get its data
    // on the disk before the target's name can point at it.
    if (!moveFile(candidate, staged, error)) {
        return false;
    }
    const auto dropStaged = [&staged] {
        boost::system::error_code removeEc;
        fs::remove(staged, removeEc);
    };

    const auto targetStatus = fs::status(target, ec);
    if (!ec) {
        fs::permissions(staged, targetStatus.permissions(), ec);
    }
    if (ec) {
        WRN("Installer: can't copy permissions of {}: {}", target.string(), ec.message());
    }
    if (!syncToDisk(staged, error)) {
        dropStaged();
        return false;
    }

    // From here on the target may change; any failure puts the original back
    bool haveBackup = false;
    const auto rollback = [&](const std::string &why) {
        std::string restoreError;
        if (!haveBackup) {
            error = fmt::format("{}; no backup to restore", why);
        } else if (moveFile(backup, target, restoreError) && syncToDisk(dir, restoreError)) {
            error = fmt::format("{}; original restored", why);
        } else {
            error = fmt::format("{}; restoring the original failed: {}", why, restoreError);
        }
        dropStaged();
        return false;
    };

    // 2. Keep the original under the backup name, and 3. swap the new binary in
#if BOOST_OS_WINDOWS
    // A loaded DLL or a running executable can't be replaced or deleted, only renamed, so the
    // original is moved aside. A backup that couldn't be removed is the image this process was
    // loaded from, by an update earlier in this session: the target isn't in use then, and is
    // replaced directly.
    if (!fs::exists(backup, ec)) {
        if (!moveFile(target, backup, error)) {
            dropStaged();
            return false;
        }
        haveBackup = true;
    }
    if (!moveFile(staged, target, error)) {
        return rollback(error);
    }
#else
    // A hard link keeps the original without copying it; a filesystem without them gets a copy
    fs::create_hard_link(target, backup, ec);
    if (ec) {
        fs::copy_file(target, backup, ec);
        if (ec) {
            error = fmt::format("Can't back up {}: {}", target.string(), ec.message());
            dropStaged();
            return false;
        }
        if (!syncToDisk(backup, error)) {
            fs::remove(backup, ec);
            dropStaged();
            return false;
        }
    }
    haveBackup = true;
    if (!syncToDisk(dir, error)) {
        fs::remove(backup, ec);
        dropStaged();
        return false;
    }

    // The rename replaces the target in one step, its name never points at nothing
    if (!moveFile(staged, target, error)) {
        fs::remove(backup, ec);
        dropStaged();
        return false;
    }
#endif

    if (!syncToDisk(dir, error)) {
        return rollback(error);
    }

    // 4. Verify what's in place now
    std::string verifyError;
    const auto installedSize = fs::file_size(target, ec);
    if (ec || installedSize != size) {
        return rollback(fmt::format("Installed size mismatch: {}, expected {}, found {}",
                                    target.string(), size, ec ? 0 : installedSize));
    }
    if (!isExecutableImage(target, verifyError)) {
        return rollback(verifyError);
    }
    if (check && !check(target)) {
        return rollback(fmt::format("Installed binary check failed: {}", target.string()));
    }

    // 5. The new binary is in place and on the disk, the original is no longer needed
    fs::remove(backup, ec);
    if (ec) {
        // On Windows it's the image this process runs from, the next update removes it
        DBG("Installer: backup kept: {}, {}", backup.string(), ec.message());
    } else if (std::string syncError; !syncToDisk(dir, syncError)) {
        WRN("Installer: {}", syncError);
    }

    return true;
}

} // namespace detail
} // namespace scorbit
