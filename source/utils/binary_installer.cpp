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

// ELF layout, the offsets of the fields read, for 32 and 64-bit images
struct ElfLayout {
    std::size_t ehdrSize, phoff, shoff, phentsize, phnum, shentsize, shnum;
    std::size_t addrSize; // size of an address or offset field
    std::size_t phdrSize, pOffset, pFilesz;
    std::size_t shdrSize;
};

constexpr ElfLayout ELF32 {52, 28, 32, 42, 44, 46, 48, 4, 32, 4, 16, 40};
constexpr ElfLayout ELF64 {64, 32, 40, 54, 56, 58, 60, 8, 56, 8, 32, 64};

constexpr std::uint32_t PT_LOAD_TYPE = 1;
constexpr std::uint16_t ET_EXEC_TYPE = 2;
constexpr std::uint16_t ET_DYN_TYPE = 3;

// Reads an unsigned field of @p size bytes in the image's byte order
std::uint64_t readField(const unsigned char *p, std::size_t size, bool bigEndian)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
        value |= std::uint64_t {p[bigEndian ? size - 1 - i : i]} << (8 * i);
    }
    return value;
}

// True if [offset, offset + length) lies within a file of @p fileSize bytes
bool fits(std::uint64_t offset, std::uint64_t length, std::uint64_t fileSize)
{
    return offset <= fileSize && length <= fileSize - offset;
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

std::optional<ElfInfo> readElfImage(const fs::path &path, std::string &error)
{
    boost::system::error_code ec;
    const std::uint64_t size = fs::file_size(path, ec);
    if (ec) {
        error = fmt::format("Can't read the size of {}: {}", path.string(), ec.message());
        return std::nullopt;
    }

    std::ifstream in(path.c_str(), std::ios::binary);
    const auto readAt = [&in](std::uint64_t offset, unsigned char *out, std::size_t length) {
        in.seekg(static_cast<std::streamoff>(offset));
        return static_cast<bool>(in.read(reinterpret_cast<char *>(out), length));
    };
    const auto fail = [&](std::string_view why) {
        error = fmt::format("Not a complete ELF image: {}, {}", path.string(), why);
        return std::nullopt;
    };

    std::array<unsigned char, ELF64.ehdrSize> ehdr {};
    if (size < 16 || !readAt(0, ehdr.data(), 16)) {
        return fail("no identification");
    }
    if (ehdr[0] != 0x7f || ehdr[1] != 'E' || ehdr[2] != 'L' || ehdr[3] != 'F') {
        return fail("bad magic");
    }

    const ElfInfo info {ehdr[4], ehdr[5], 0};
    if ((info.elfClass != 1 && info.elfClass != 2) || (info.byteOrder != 1 && info.byteOrder != 2)
        || ehdr[6] != 1) {
        return fail("unknown class, byte order or version");
    }
    const auto &layout = info.elfClass == 2 ? ELF64 : ELF32;
    const bool bigEndian = info.byteOrder == 2;
    const auto field = [bigEndian](const unsigned char *p, std::size_t offset, std::size_t len) {
        return readField(p + offset, len, bigEndian);
    };

    if (size < layout.ehdrSize || !readAt(0, ehdr.data(), layout.ehdrSize)) {
        return fail("header cut short");
    }

    const auto type = field(ehdr.data(), 16, 2);
    if (type != ET_EXEC_TYPE && type != ET_DYN_TYPE) {
        return fail("neither an executable nor a shared object");
    }
    const auto machine = static_cast<std::uint16_t>(field(ehdr.data(), 18, 2));

    // Program headers: there must be some, all within the file, and so must every loadable
    // segment they describe
    const auto phoff = field(ehdr.data(), layout.phoff, layout.addrSize);
    const auto phentsize = field(ehdr.data(), layout.phentsize, 2);
    const auto phnum = field(ehdr.data(), layout.phnum, 2);
    if (phnum == 0 || phentsize != layout.phdrSize || !fits(phoff, phnum * phentsize, size)) {
        return fail("program headers missing or past the end of the file");
    }

    std::array<unsigned char, ELF64.phdrSize> phdr {};
    for (std::uint64_t i = 0; i < phnum; ++i) {
        if (!readAt(phoff + i * phentsize, phdr.data(), layout.phdrSize)) {
            return fail("can't read a program header");
        }
        if (field(phdr.data(), 0, 4) != PT_LOAD_TYPE) {
            continue;
        }
        const auto offset = field(phdr.data(), layout.pOffset, layout.addrSize);
        const auto filesz = field(phdr.data(), layout.pFilesz, layout.addrSize);
        if (!fits(offset, filesz, size)) {
            return fail("a loadable segment is past the end of the file");
        }
    }

    // Section headers usually come last in the file, so they show a cut-off tail too
    const auto shoff = field(ehdr.data(), layout.shoff, layout.addrSize);
    const auto shentsize = field(ehdr.data(), layout.shentsize, 2);
    const auto shnum = field(ehdr.data(), layout.shnum, 2);
    if (shoff != 0
        && (shentsize != layout.shdrSize
            || !fits(shoff, std::max<std::uint64_t>(shnum, 1) * shentsize, size))) {
        return fail("section headers past the end of the file");
    }

    return ElfInfo {info.elfClass, info.byteOrder, machine};
}

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

#if !BOOST_OS_WINDOWS && !BOOST_OS_MACOS
    if (!readElfImage(path, error)) {
        return false;
    }
#endif

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

    // Without a free backup name there would be nothing to roll back to. On Windows a backup that
    // can't be removed is the image this process runs from, after an update earlier in this
    // session; the next update after a restart removes it.
    if (fs::exists(backup, ec)) {
        error = fmt::format("Can't remove the previous backup, a restart may be needed: {}",
                            backup.string());
        return false;
    }

    // Moving the target onto its staging name would leave nothing to back up
    if (fs::equivalent(candidate, target, ec)) {
        error = fmt::format("The new binary is the current one: {}", candidate.string());
        return false;
    }

    if (!isExecutableImage(candidate, error)) {
        return false;
    }

#if !BOOST_OS_WINDOWS && !BOOST_OS_MACOS
    // A binary for another architecture would not load. A target that can't be read gives
    // nothing to compare with, the update may replace it.
    if (std::string targetError; const auto current = readElfImage(target, targetError)) {
        if (const auto incoming = readElfImage(candidate, error); incoming != current) {
            error = fmt::format("Built for another architecture: {}, machine {}, expected {}",
                                candidate.string(), incoming ? incoming->machine : 0,
                                current->machine);
            return false;
        }
    }
#endif

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

    // Once the backup is made the target may change; any failure puts the original back
    const auto rollback = [&](const std::string &why) {
        std::string restoreError;
        if (moveFile(backup, target, restoreError) && syncToDisk(dir, restoreError)) {
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
    // original is moved aside. A crash between the two moves leaves the target missing: only a
    // launcher that runs before the binary loads could recover from that.
    if (!moveFile(target, backup, error)) {
        dropStaged();
        return false;
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
