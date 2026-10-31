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

// Reads a Visual Pinball X table (.vpx, an OLE/MS-CFB compound file) into table_signature::Inputs
// and computes its vts1 signature. Header-only, C++17, no dependencies beyond the standard library
// and the OS file API. The file is untrusted: every read is bounds-checked and capped, nothing
// recurses on file data, and the entry points are noexcept. See README.md.

#pragma once

#include "inflate.h"
#include "table_signature.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <cerrno>
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#endif

namespace table_signature::vpx {

/// Values are fixed and only ever appended to, since hosts may store them.
enum class Code : int {
    ok = 0,
    table_unreadable_permission = 1, ///< the table or a script it needs could not be opened:
                                     ///< EACCES/EPERM
    table_unreadable = 2,            ///< any other open or read failure
    legacy_protected = 3,    ///< the embedded script is encrypted by VPX's legacy table protection
    unnamed_helper = 4,      ///< compute() refused: the script loads a helper by a non-literal name
    missing_helper = 5,      ///< a helper named by a literal is found nowhere (or is not followed);
                             ///< VPX would fail to load it at run time. Names in missingHelpers.
    signature_too_large = 6, ///< a size or count cap was reached
    malformed = 7,           ///< not a well-formed compound file or BIFF data
    cancelled = 8,
};

inline auto codeName(Code c) noexcept -> const char *
{
    switch (c) {
    case Code::ok:
        return "ok";
    case Code::table_unreadable_permission:
        return "table_unreadable_permission";
    case Code::table_unreadable:
        return "table_unreadable";
    case Code::legacy_protected:
        return "legacy_protected";
    case Code::unnamed_helper:
        return "unnamed_helper";
    case Code::missing_helper:
        return "missing_helper";
    case Code::signature_too_large:
        return "signature_too_large";
    case Code::malformed:
        return "malformed";
    case Code::cancelled:
        return "cancelled";
    }
    return "malformed";
}

/// Caps on what one table may make the reader read or allocate.
struct Limits {
    uint64_t maxFileSize = uint64_t(4) << 30;
    uint64_t maxStreamSize = uint64_t(64) << 20; ///< one CFB stream, sidecar or helper script
    uint64_t maxTotalRead = uint64_t(256) << 20; ///< all streams and scripts together
    uint64_t maxMeshBytes = uint64_t(64) << 20;  ///< one inflated vertex or index buffer
    uint32_t maxDirectoryEntries = 1u << 16;
    uint32_t maxStreams = 1u << 15;    ///< GameItem and Collection streams
    uint32_t maxRecords = 1u << 20;    ///< BIFF records in one stream
    uint32_t maxDragPoints = 1u << 16; ///< per item
    uint32_t maxHelperScripts = 256;
    /// The script, sidecar and helpers together: they are tokenized, at most one token per byte.
    uint64_t maxScriptBytes = uint64_t(2) << 20;
    uint64_t maxScriptTokens = uint64_t(1) << 20; ///< tokens over all scripts scanned
    uint64_t maxListedEntries = uint64_t(1) << 17; ///< folder entries listed by the script search
};

struct Options {
    Limits limits;
    /// Checked between streams and between script files; set it to stop with Code::cancelled.
    const std::atomic<bool> *cancel = nullptr;
};

/// What the .vpx alone holds. inputs.script is the embedded CODE record; no helpers are resolved.
struct Contents {
    Code code = Code::malformed;
    Inputs inputs;
    /// The CODE record is encrypted (legacy SECB protection); inputs.script is then unusable.
    bool scriptProtected = false;
};

/// The folders VPX searches for scripts, in its order: the table's, VPX's install folder, then
/// VPX's preferences folder; in each the folder itself, then user/, scripts/ (and tables/ in the
/// install folder). Empty folders are skipped.
struct Paths {
    std::string table;          ///< the .vpx, UTF-8
    std::string tableDirectory; ///< the table's folder; empty: the folder the table is in
    std::string vpxDirectory;   ///< VPX's install folder (its app path); empty: not searched
    std::string prefDirectory;  ///< VPX's preferences folder (its pref path); empty: not searched
};

struct Result {
    Code code = Code::malformed;
    Signature signature;                     ///< valid only when code is ok
    Inputs inputs; ///< what was hashed: filled for ok, unnamed_helper and missing_helper, else empty
    /// Literal helper names found nowhere, or with a path that is not followed; any makes the
    /// code missing_helper.
    std::vector<std::string> missingHelpers;
    bool sidecar = false; ///< the script came from a .vbs file, not the CODE record
};

namespace detail {

constexpr uint32_t kMaxRegSect = 0xFFFFFFFA;
constexpr uint32_t kEndOfChain = 0xFFFFFFFE;
constexpr uint32_t kNoStream = 0xFFFFFFFF;
constexpr uint32_t kDirEntrySize = 128;
constexpr uint32_t kMiniSectorSize = 64;
constexpr uint32_t kMiniCutoff = 4096;
// The item types that write a FONT record (ItemTypeEnum, vpinball src/core/iselect.h).
constexpr uint32_t kTextbox = 4;
constexpr uint32_t kDecal = 9;

/// Thrown internally only; the noexcept entry points turn it into a Code.
struct Failure {
    Code code;
};

[[noreturn]] inline void fail(Code c)
{
    throw Failure {c};
}

inline auto le16(const uint8_t *p) -> uint32_t
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8);
}
inline auto le32(const uint8_t *p) -> uint32_t
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline auto lowerAscii(std::string s) -> std::string
{
    for (auto &c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

inline void appendUtf8(std::string &out, uint32_t cp)
{
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

/// UTF-16LE to UTF-8; an unpaired surrogate becomes U+FFFD.
inline auto utf16ToUtf8(const uint8_t *p, size_t units) -> std::string
{
    std::string out;
    for (size_t i = 0; i < units; i++) {
        uint32_t u = le16(p + 2 * i);
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < units) {
            const uint32_t lo = le16(p + 2 * (i + 1));
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                appendUtf8(out, 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00));
                i++;
                continue;
            }
        }
        appendUtf8(out, (u >= 0xD800 && u <= 0xDFFF) ? 0xFFFD : u);
    }
    return out;
}

// ---------------------------------------------------------------- byte sources

class Source
{
public:
    virtual ~Source() = default;
    virtual auto size() const -> uint64_t = 0;
    /// Reads exactly n bytes at off, or returns false (caller has checked off + n <= size()).
    virtual auto read(uint64_t off, uint8_t *dst, size_t n) -> bool = 0;
};

class MemorySource : public Source
{
public:
    MemorySource(const uint8_t *data, size_t size)
        : m_data(data)
        , m_size(size)
    {
    }
    auto size() const -> uint64_t override { return m_size; }
    auto read(uint64_t off, uint8_t *dst, size_t n) -> bool override
    {
        if (off > m_size || n > m_size - off) {
            return false;
        }
        if (n != 0) {
            std::memcpy(dst, m_data + off, n);
        }
        return true;
    }

private:
    const uint8_t *m_data;
    size_t m_size;
};

/// Read-only, never locking: other processes (VPX itself) may keep reading and writing the file.
class FileSource : public Source
{
public:
    ~FileSource() override { close(); }

    /// noFollow: refuse a symbolic link or other reparse point as the file itself.
    auto open(const std::string &utf8Path, bool noFollow = false) -> Code
    {
#ifdef _WIN32
        const std::wstring wide = widen(utf8Path);
        if (wide.empty() && !utf8Path.empty()) {
            return Code::table_unreadable;
        }
        m_handle = CreateFileW(wide.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | (noFollow ? FILE_FLAG_OPEN_REPARSE_POINT : 0),
                               nullptr);
        if (m_handle == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_ACCESS_DENIED) {
                return Code::table_unreadable;
            }
            // Opening a folder is refused as access denied too; that is not a permission problem.
            const DWORD attributes = GetFileAttributesW(wide.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
                           ? Code::table_unreadable
                           : Code::table_unreadable_permission;
        }
        LARGE_INTEGER size;
        BY_HANDLE_FILE_INFORMATION info;
        if (GetFileType(m_handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(m_handle, &info)
            || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
            || (noFollow && (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            || !GetFileSizeEx(m_handle, &size) || size.QuadPart < 0) {
            return Code::table_unreadable;
        }
        m_size = static_cast<uint64_t>(size.QuadPart);
#else
        // O_NONBLOCK: a FIFO put in the file's place must not block the open; S_ISREG refuses it.
        int flags = O_RDONLY | O_NONBLOCK | (noFollow ? O_NOFOLLOW : 0);
#    ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#    endif
        m_fd = ::open(utf8Path.c_str(), flags);
        if (m_fd < 0) {
            return (errno == EACCES || errno == EPERM) ? Code::table_unreadable_permission
                                                       : Code::table_unreadable;
        }
        struct stat st {
        };
        if (::fstat(m_fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) {
            return Code::table_unreadable;
        }
        m_size = static_cast<uint64_t>(st.st_size);
#endif
        return Code::ok;
    }

    auto size() const -> uint64_t override
    {
        return m_size;
    }

    auto read(uint64_t off, uint8_t *dst, size_t n) -> bool override
    {
        while (n > 0) {
#ifdef _WIN32
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(n, 1u << 30));
            OVERLAPPED ov {};
            ov.Offset = static_cast<DWORD>(off);
            ov.OffsetHigh = static_cast<DWORD>(off >> 32);
            DWORD got = 0;
            if (!ReadFile(m_handle, dst, chunk, &got, &ov) || got == 0) {
                return false;
            }
#else
            if (off > static_cast<uint64_t>(std::numeric_limits<off_t>::max())) {
                return false;
            }
            const size_t chunk = std::min<size_t>(n, size_t(1) << 30);
            const ssize_t got = ::pread(m_fd, dst, chunk, static_cast<off_t>(off));
            if (got < 0 && errno == EINTR) {
                continue;
            }
            if (got <= 0) {
                return false;
            }
#endif
            dst += got;
            off += static_cast<uint64_t>(got);
            n -= static_cast<size_t>(got);
        }
        return true;
    }

#ifdef _WIN32
    static auto widen(const std::string &s) -> std::wstring
    {
        if (s.empty() || s.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return {};
        }
        const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()),
                                          nullptr, 0);
        if (n <= 0) {
            return {};
        }
        std::wstring w(static_cast<size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), &w[0], n);
        return w;
    }
#endif

private:
    void close()
    {
#ifdef _WIN32
        if (m_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
#else
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
#endif
    }

#ifdef _WIN32
    HANDLE m_handle = INVALID_HANDLE_VALUE;
#else
    int m_fd = -1;
#endif
    uint64_t m_size = 0;
};

// ---------------------------------------------------------------- compound file (MS-CFB)

struct DirEntry {
    std::string name; // UTF-8
    uint8_t type = 0; // 0 unused, 1 storage, 2 stream, 5 root
    uint32_t left = kNoStream, right = kNoStream, child = kNoStream;
    uint32_t start = kEndOfChain;
    uint64_t size = 0;
};

/// Counts bytes read against Limits::maxTotalRead, before they are allocated.
inline void charge(uint64_t &total, uint64_t bytes, const Limits &limits)
{
    if (bytes > limits.maxTotalRead - (std::min)(total, limits.maxTotalRead)) {
        fail(Code::signature_too_large);
    }
    total += bytes;
}

class Cfb
{
public:
    Cfb(Source &src, const Options &opt, uint64_t &total)
        : m_src(src)
        , m_opt(opt)
        , m_total(total)
    {
    }

    void open()
    {
        const uint64_t fileSize = m_src.size();
        if (fileSize > m_opt.limits.maxFileSize) {
            fail(Code::signature_too_large);
        }
        if (fileSize < 512) {
            fail(Code::malformed);
        }
        uint8_t h[512];
        if (!m_src.read(0, h, sizeof h)) {
            fail(Code::table_unreadable);
        }
        static const uint8_t sig[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
        const uint32_t major = le16(h + 0x1A);
        const uint32_t shift = le16(h + 0x1E);
        if (std::memcmp(h, sig, 8) != 0 || le16(h + 0x1C) != 0xFFFE || le16(h + 0x20) != 6
            || !((major == 3 && shift == 9) || (major == 4 && shift == 12))
            || le32(h + 0x38) != kMiniCutoff) {
            fail(Code::malformed);
        }
        m_sectorSize = 1u << shift;
        m_major = major;
        if (fileSize < m_sectorSize) {
            fail(Code::malformed);
        }
        // Sectors that start inside the file (the last may be short); the header fills slot -1.
        m_fileSectors = count(fileSize - m_sectorSize, m_sectorSize);
        const uint32_t perSector = m_sectorSize / 4;
        const uint32_t numFat = le32(h + 0x2C);
        if (numFat == 0 || numFat > m_fileSectors / perSector + 1) {
            fail(Code::malformed);
        }
        m_firstDir = le32(h + 0x30);
        m_firstMiniFat = le32(h + 0x3C);
        m_numMiniFat = le32(h + 0x40);
        const uint32_t numDifat = le32(h + 0x48);

        // DIFAT: 109 entries in the header, then a chain of DIFAT sectors.
        std::vector<uint32_t> fatSectors;
        for (uint32_t i = 0; i < 109 && fatSectors.size() < numFat; i++) {
            fatSectors.push_back(le32(h + 0x4C + 4 * i));
        }
        uint32_t difat = le32(h + 0x44);
        std::vector<uint8_t> sector(m_sectorSize);
        std::vector<bool> seenDifat;
        for (uint32_t n = 0; fatSectors.size() < numFat; n++) {
            if (n >= numDifat || difat > kMaxRegSect || difat >= m_fileSectors) {
                fail(Code::malformed);
            }
            if (seenDifat.empty()) {
                seenDifat.assign(static_cast<size_t>(m_fileSectors), false);
            }
            if (seenDifat[difat]) {
                fail(Code::malformed);
            }
            seenDifat[difat] = true;
            readSector(difat, sector.data());
            for (uint32_t i = 0; i + 1 < perSector && fatSectors.size() < numFat; i++) {
                fatSectors.push_back(le32(sector.data() + 4 * i));
            }
            difat = le32(sector.data() + 4 * (perSector - 1));
        }
        m_fatSectors = std::move(fatSectors);
        m_fatEntries = uint64_t(m_fatSectors.size()) * perSector;
        readDirectory();
    }

    auto entries() const -> const std::vector<DirEntry> & { return m_dir; }

    /// Children of a storage, by an iterative walk of its sibling tree; a revisit is a cycle.
    auto children(uint32_t storage) const -> std::vector<uint32_t>
    {
        std::vector<uint32_t> out;
        std::vector<bool> seen(m_dir.size(), false);
        std::vector<uint32_t> stack;
        if (m_dir[storage].child != kNoStream) {
            stack.push_back(m_dir[storage].child);
        }
        while (!stack.empty()) {
            const uint32_t i = stack.back();
            stack.pop_back();
            if (i >= m_dir.size() || seen[i] || i == 0 || m_dir[i].type == 0) {
                fail(Code::malformed);
            }
            seen[i] = true;
            out.push_back(i);
            for (const uint32_t next : {m_dir[i].left, m_dir[i].right}) {
                if (next != kNoStream) {
                    stack.push_back(next);
                }
            }
        }
        return out;
    }

    auto find(uint32_t storage, const std::string &name) const -> uint32_t
    {
        for (const uint32_t i : children(storage)) {
            if (lowerAscii(m_dir[i].name) == lowerAscii(name)) {
                return i;
            }
        }
        return kNoStream;
    }

    /// A stream's bytes; counts against the per-stream and total caps before allocating.
    auto readStream(uint32_t index) -> std::vector<uint8_t>
    {
        const DirEntry &e = m_dir[index];
        if (e.type != 2) {
            fail(Code::malformed);
        }
        if (e.size > m_opt.limits.maxStreamSize || e.size > m_src.size()) {
            fail(e.size > m_opt.limits.maxStreamSize ? Code::signature_too_large : Code::malformed);
        }
        charge(e.size);
        std::vector<uint8_t> out(static_cast<size_t>(e.size));
        if (e.size == 0) {
            return out;
        }
        if (e.size < kMiniCutoff) {
            readMini(e.start, out);
        } else {
            const auto sectors = chain(true, e.start, count(e.size, m_sectorSize), true);
            size_t done = 0;
            for (const uint32_t sid : sectors) {
                const size_t n = std::min<size_t>(m_sectorSize, out.size() - done);
                readSectorPart(sid, 0, out.data() + done, n);
                done += n;
            }
        }
        return out;
    }

    void charge(uint64_t bytes) { detail::charge(m_total, bytes, m_opt.limits); }

private:
    static auto count(uint64_t bytes, uint32_t unit) -> uint64_t
    {
        return bytes / unit + (bytes % unit != 0);
    }

    void readSectorPart(uint32_t sid, uint32_t offset, uint8_t *dst, size_t n)
    {
        const uint64_t at = (uint64_t(sid) + 1) * m_sectorSize + offset; // sid < 2^32: no overflow
        if (sid > kMaxRegSect || offset > m_sectorSize || n > m_sectorSize - offset
            || at > m_src.size() || n > m_src.size() - at) {
            fail(Code::malformed);
        }
        if (!m_src.read(at, dst, n)) {
            fail(Code::table_unreadable);
        }
    }

    void readSector(uint32_t sid, uint8_t *dst) { readSectorPart(sid, 0, dst, m_sectorSize); }

    /// Follows a chain from start. exact: exactly `want` sectors (more are ignored, fewer is an
    /// error); otherwise to ENDOFCHAIN, failing past `want`. A revisited sector is a cycle.
    /// The FAT is read a sector at a time as chains reach it, so only the parts in use are read.
    auto fatEntry(uint32_t sid) -> uint32_t
    {
        const uint32_t perSector = m_sectorSize / 4;
        const uint32_t page = sid / perSector;
        auto it = m_fatPages.find(page);
        if (it == m_fatPages.end()) {
            std::vector<uint8_t> raw(m_sectorSize);
            readSector(m_fatSectors[page], raw.data());
            std::vector<uint32_t> entries(perSector);
            for (uint32_t i = 0; i < perSector; i++) {
                entries[i] = le32(raw.data() + 4 * i);
            }
            it = m_fatPages.emplace(page, std::move(entries)).first;
        }
        return it->second[sid % perSector];
    }

    /// Follows a FAT (or mini FAT) chain from start. exact: exactly `want` sectors (more are
    /// ignored, fewer is an error); otherwise to ENDOFCHAIN, failing past `want`. The walk never
    /// exceeds `want` steps, and a sector met twice is a cycle.
    auto chain(bool fat, uint32_t start, uint64_t want, bool exact) -> std::vector<uint32_t>
    {
        const uint64_t tableSize = fat ? m_fatEntries : m_miniFat.size();
        if (want > tableSize) {
            fail(Code::malformed); // more sectors than the table (and so the file) can hold
        }
        std::vector<uint32_t> out;
        uint32_t sid = start;
        while (sid != kEndOfChain) {
            if (out.size() == want) {
                if (exact) {
                    fail(Code::malformed); // the chain runs on past its size: MS-CFB ends it here
                }
                const bool cycle = std::find(out.begin(), out.end(), sid) != out.end();
                fail(cycle ? Code::malformed : Code::signature_too_large);
            }
            if (sid >= tableSize) {
                fail(Code::malformed);
            }
            out.push_back(sid);
            sid = fat ? fatEntry(sid) : m_miniFat[sid];
        }
        if (exact && out.size() != want) {
            fail(Code::malformed);
        }
        std::vector<uint32_t> sorted = out;
        std::sort(sorted.begin(), sorted.end());
        if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
            fail(Code::malformed);
        }
        return out;
    }

    void readDirectory()
    {
        const uint32_t perSector = m_sectorSize / kDirEntrySize;
        const uint64_t maxSectors = count(m_opt.limits.maxDirectoryEntries, perSector);
        const auto sectors =
                chain(true, m_firstDir, std::min<uint64_t>(maxSectors, m_fatEntries), false);
        std::vector<uint8_t> s(m_sectorSize);
        for (const uint32_t sid : sectors) {
            readSector(sid, s.data());
            for (uint32_t k = 0; k < perSector; k++) {
                const uint8_t *p = s.data() + k * kDirEntrySize;
                DirEntry e;
                e.type = p[66];
                const uint32_t nameBytes = le16(p + 64);
                if (e.type != 0) {
                    if (nameBytes > 64 || nameBytes % 2 != 0
                        || (e.type != 1 && e.type != 2 && e.type != 5)) {
                        fail(Code::malformed);
                    }
                    size_t units = nameBytes / 2;
                    if (units > 0 && le16(p + 2 * (units - 1)) == 0) {
                        units--; // drop the terminator
                    }
                    e.name = utf16ToUtf8(p, units);
                }
                e.left = le32(p + 68);
                e.right = le32(p + 72);
                e.child = le32(p + 76);
                e.start = le32(p + 116);
                // Version 3 files may leave garbage in the high half of the size (MS-CFB 2.6.3).
                e.size = le32(p + 120) | (m_major == 4 ? uint64_t(le32(p + 124)) << 32 : 0);
                m_dir.push_back(std::move(e));
            }
        }
        if (m_dir.empty() || m_dir[0].type != 5) {
            fail(Code::malformed);
        }
    }

    void loadMini()
    {
        if (m_miniLoaded) {
            return;
        }
        const DirEntry &root = m_dir[0];
        // Only streams under 4096 bytes live in the mini stream; a huge one is not a real table.
        if (root.size > m_opt.limits.maxStreamSize) {
            fail(Code::signature_too_large);
        }
        m_miniContainer = chain(true, root.start, count(root.size, m_sectorSize), true);
        // The header says how many sectors the mini FAT has; a writer may over-allocate it. Walk
        // that chain exactly (chain() refuses more than the file holds), then load only the
        // sectors the mini stream needs, so a huge count costs no memory.
        const uint64_t miniSectors = count(root.size, kMiniSectorSize);
        const uint64_t needed = count(miniSectors * 4, m_sectorSize);
        if (uint64_t(m_numMiniFat) < needed) {
            fail(Code::malformed);
        }
        // More than a mini stream of maxStreamSize could need is not a table this reader takes.
        if (uint64_t(m_numMiniFat)
            > count(count(m_opt.limits.maxStreamSize, kMiniSectorSize) * 4, m_sectorSize)) {
            fail(Code::signature_too_large);
        }
        auto sectors = chain(true, m_firstMiniFat, m_numMiniFat, true);
        sectors.resize(static_cast<size_t>(needed));
        std::vector<uint8_t> s(m_sectorSize);
        for (const uint32_t sid : sectors) {
            readSector(sid, s.data());
            for (uint32_t i = 0; i < m_sectorSize / 4; i++) {
                m_miniFat.push_back(le32(s.data() + 4 * i));
            }
        }
        m_miniLoaded = true;
    }

    void readMini(uint32_t start, std::vector<uint8_t> &out)
    {
        loadMini();
        const auto minis = chain(false, start, count(out.size(), kMiniSectorSize), true);
        size_t done = 0;
        for (const uint32_t mid : minis) {
            const uint64_t byteOffset = uint64_t(mid) * kMiniSectorSize;
            const size_t n = std::min<size_t>(kMiniSectorSize, out.size() - done);
            if (byteOffset + n > m_dir[0].size) {
                fail(Code::malformed);
            }
            const uint64_t index = byteOffset / m_sectorSize;
            if (index >= m_miniContainer.size()) {
                fail(Code::malformed);
            }
            readSectorPart(m_miniContainer[static_cast<size_t>(index)],
                           uint32_t(byteOffset % m_sectorSize), out.data() + done, n);
            done += n;
        }
    }

    Source &m_src;
    const Options &m_opt;
    uint32_t m_sectorSize = 512;
    uint32_t m_major = 3;
    uint64_t m_fileSectors = 0;
    uint32_t m_firstDir = kEndOfChain;
    uint32_t m_firstMiniFat = kEndOfChain;
    uint32_t m_numMiniFat = 0;
    std::vector<uint32_t> m_fatSectors;
    uint64_t m_fatEntries = 0;
    std::map<uint32_t, std::vector<uint32_t>> m_fatPages;
    std::vector<DirEntry> m_dir;
    bool m_miniLoaded = false;
    std::vector<uint32_t> m_miniContainer;
    std::vector<uint32_t> m_miniFat;
    uint64_t &m_total;
};

// ---------------------------------------------------------------- BIFF records

struct Record {
    std::string tag;
    const uint8_t *data;
    size_t size;
};

/// Walks BIFF records: int32 length (tag + data), 4-byte tag, data. Two records have a length that
/// covers only their tag, with untagged data after it: CODE in GameData (an int32 byte count, then
/// the script) and FONT in a textbox or decal (version 1, charset 2, attributes 1, weight 2, size 4,
/// name length 1, then the name; VPX's WriteFontDescriptor).
class Biff
{
public:
    Biff(const std::vector<uint8_t> &bytes, size_t start, uint32_t maxRecords,
         bool codeRecord = false, bool fontRecord = false)
        : m_p(bytes.data())
        , m_size(bytes.size())
        , m_pos(std::min(start, bytes.size()))
        , m_max(maxRecords)
        , m_code(codeRecord)
        , m_font(fontRecord)
    {
    }

    /// The next record, or false at the end of the data. A length past the end is malformed.
    auto next(Record &r) -> bool
    {
        if (m_pos == m_size) {
            return false;
        }
        if (++m_count > m_max) {
            fail(Code::signature_too_large);
        }
        if (m_size - m_pos < 8) {
            fail(Code::malformed);
        }
        const uint32_t len = le32(m_p + m_pos);
        if (len < 4 || len > 0x7FFFFFFF || len - 4 > m_size - m_pos - 8) {
            fail(Code::malformed);
        }
        r.tag.assign(reinterpret_cast<const char *>(m_p + m_pos + 4), 4);
        r.data = m_p + m_pos + 8;
        r.size = len - 4;
        m_pos += 8 + r.size;
        if (m_code && r.tag == "CODE" && r.size == 0) {
            if (m_size - m_pos < 4) {
                fail(Code::malformed);
            }
            const uint32_t n = le32(m_p + m_pos);
            if (n > 0x7FFFFFFF || n > m_size - m_pos - 4) {
                fail(Code::malformed);
            }
            r.data = m_p + m_pos + 4;
            r.size = n;
            m_pos += 4 + n;
        } else if (m_font && r.tag == "FONT" && r.size == 0) {
            constexpr size_t kFixed = 11; // the descriptor up to and including the name length
            if (m_size - m_pos < kFixed) {
                fail(Code::malformed);
            }
            const size_t n = kFixed + m_p[m_pos + kFixed - 1];
            if (n > m_size - m_pos) {
                fail(Code::malformed);
            }
            r.data = m_p + m_pos;
            r.size = n;
            m_pos += n;
        }
        return true;
    }

private:
    const uint8_t *m_p;
    size_t m_size;
    size_t m_pos;
    uint32_t m_max;
    uint32_t m_count = 0;
    bool m_code;
    bool m_font;
};

// What one parsed record or string is charged to maxTotalRead on top of its bytes: the object and
// its heap blocks, so many tiny records cannot add up to far more memory than the cap.
constexpr uint64_t kParsedOverhead = sizeof(Field) + 64;

inline auto field(const Record &r, uint64_t &total, const Limits &limits) -> Field
{
    charge(total, kParsedOverhead + r.size, limits);
    return {r.tag, std::vector<uint8_t>(r.data, r.data + r.size)};
}

/// A WriteWideString value: int32 byte count, then UTF-16LE.
inline auto wideString(const Record &r) -> std::string
{
    if (r.size < 4) {
        fail(Code::malformed);
    }
    const uint32_t bytes = le32(r.data);
    if (bytes > r.size - 4 || bytes % 2 != 0) {
        fail(Code::malformed);
    }
    return utf16ToUtf8(r.data + 4, bytes / 2);
}

inline auto int32Value(const Field *f) -> int64_t
{
    return (f != nullptr && f->value.size() >= 4) ? int64_t(int32_t(le32(f->value.data()))) : -1;
}

inline auto lastField(const std::vector<Field> &fields, const char *tag) -> const Field *
{
    for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
        if (it->tag == tag) {
            return &*it;
        }
    }
    return nullptr;
}

/// A primitive's own mesh: x, y, z of each Vertex3D_NoTex2 (32 bytes) and its indices, 16-bit up
/// to 65535 vertices. Compressed forms (M3CX/M3CI) are zlib streams.
inline void readMesh(Item &item, const Limits &limits, uint64_t &total)
{
    const Field *u3dm = lastField(item.fields, "U3DM");
    if (u3dm == nullptr || u3dm->value.size() < 4 || le32(u3dm->value.data()) == 0) {
        return;
    }
    const int64_t vertices = int32Value(lastField(item.fields, "M3VN"));
    const int64_t indices = int32Value(lastField(item.fields, "M3FN"));
    if (vertices < 0 || indices < 0) {
        return; // no mesh written; nothing to hash
    }
    const uint64_t width = vertices > 65535 ? 4 : 2;
    const uint64_t vertexBytes = uint64_t(vertices) * 32; // both < 2^31, no overflow
    const uint64_t indexBytes = uint64_t(indices) * width;
    if (vertexBytes > limits.maxMeshBytes || indexBytes > limits.maxMeshBytes) {
        fail(Code::signature_too_large);
    }
    // Inflated meshes count toward maxTotalRead (a small zlib field can expand ~1000-fold), and so
    // do the positions and 32-bit indices kept from them.
    charge(total, vertexBytes + indexBytes + uint64_t(vertices) * 12 + uint64_t(indices) * 4, limits);
    auto load = [&](const char *raw, const char *packed, uint64_t want) -> std::vector<uint8_t> {
        if (const Field *f = lastField(item.fields, packed)) {
            std::vector<uint8_t> out;
            const auto s =
                    inflate::zlib(f->value.data(), f->value.size(), static_cast<size_t>(want), out);
            if (s != inflate::Status::ok || out.size() != want) {
                fail(Code::malformed);
            }
            return out;
        }
        if (const Field *f = lastField(item.fields, raw)) {
            if (f->value.size() != want) {
                fail(Code::malformed);
            }
            return f->value;
        }
        if (want != 0) {
            fail(Code::malformed); // counts with no data
        }
        return {};
    };
    const auto v = load("M3DX", "M3CX", vertexBytes);
    const auto ix = load("M3DI", "M3CI", indexBytes);
    item.mesh.positions.reserve(v.size() / 32 * 3);
    for (size_t off = 0; off + 32 <= v.size(); off += 32) {
        for (size_t k = 0; k < 3; k++) {
            const uint32_t bits = le32(v.data() + off + 4 * k);
            float f;
            std::memcpy(&f, &bits, 4);
            item.mesh.positions.push_back(f);
        }
    }
    item.mesh.indices.reserve(ix.size() / width);
    for (size_t off = 0; off + width <= ix.size(); off += width) {
        item.mesh.indices.push_back(width == 4 ? le32(ix.data() + off) : le16(ix.data() + off));
    }
}

/// One GameItemN stream: item type, then records to ENDB. Drag points are a flat state machine,
/// not recursion: DPNT opens a point and the next ENDB closes it; a DPNT inside a point is just
/// one of its fields, as VPX's drag point loader would skip it.
inline auto readItem(const std::vector<uint8_t> &bytes, const Limits &limits, uint64_t &total)
        -> Item
{
    if (bytes.size() < 4) {
        fail(Code::malformed);
    }
    Item item;
    item.type = le32(bytes.data());
    Biff biff(bytes, 4, limits.maxRecords, false, item.type == kTextbox || item.type == kDecal);
    Record r;
    bool inPoint = false;
    DragPoint point;
    bool ended = false;
    while (!ended && biff.next(r)) {
        if (inPoint) {
            if (r.tag == "ENDB") {
                item.dragPoints.push_back(std::move(point));
                point.clear();
                inPoint = false;
            } else {
                point.push_back(field(r, total, limits));
            }
        } else if (r.tag == "ENDB") {
            ended = true;
        } else if (r.tag == "DPNT") {
            if (item.dragPoints.size() >= limits.maxDragPoints) {
                fail(Code::signature_too_large);
            }
            charge(total, kParsedOverhead, limits);
            inPoint = true;
        } else {
            if (r.tag == "NAME") {
                item.name = wideString(r);
                charge(total, item.name.size(), limits); // UTF-8 may be longer than the UTF-16
            }
            item.fields.push_back(field(r, total, limits));
        }
    }
    if (inPoint || !ended) {
        fail(Code::malformed); // a point or the item runs to the end of the stream without ENDB
    }
    if (item.type == kPrimitive) {
        readMesh(item, limits, total);
    }
    return item;
}

inline auto readCollection(const std::vector<uint8_t> &bytes, const Limits &limits, uint64_t &total)
        -> Collection
{
    Collection c;
    Biff biff(bytes, 0, limits.maxRecords);
    Record r;
    bool ended = false;
    while (!ended && biff.next(r)) {
        if (r.tag == "ENDB") {
            ended = true;
        } else if (r.tag == "NAME") {
            c.name = wideString(r);
            charge(total, c.name.size(), limits);
        } else if (r.tag == "ITEM") {
            auto member = wideString(r);
            charge(total, kParsedOverhead + r.size + member.size(), limits);
            c.members.push_back(std::move(member));
        } else {
            c.fields.push_back(field(r, total, limits));
        }
    }
    if (!ended) {
        fail(Code::malformed);
    }
    return c;
}

// VPX's legacy table protection (ProtectionData in the SECB record): int32 fileversion, int32
// size, 24-byte paraphrase, uint32 flags. With script editing disabled the CODE record is
// encrypted.
constexpr uint32_t kDisableScriptEditing = 0x00000002;
constexpr uint32_t kDisableEverything = 0x80000000;
constexpr size_t kSavePhysicsMaterialSize = 48; // char name[32], 4 floats

inline void readGameData(const std::vector<uint8_t> &bytes, Contents &c, const Limits &limits,
                         uint64_t &total)
{
    Biff biff(bytes, 0, limits.maxRecords, true);
    Record r;
    bool ended = false;
    while (!ended && biff.next(r)) {
        if (r.tag == "ENDB") {
            ended = true;
            continue;
        }
        if (r.tag == "CODE") {
            // VPX keeps the script as a C string, so it ends at the first NUL.
            const auto *p = reinterpret_cast<const char *>(r.data);
            charge(total, r.size, limits);
            c.inputs.script.assign(p, std::find(p, p + r.size, '\0'));
            continue;
        }
        if (r.tag == "SECB" && r.size >= 36) {
            const uint32_t flags = le32(r.data + 32);
            c.scriptProtected = (flags & (kDisableScriptEditing | kDisableEverything)) != 0;
        } else if (r.tag == "PHMA") {
            c.inputs.materials.clear();
            for (size_t off = 0; off + kSavePhysicsMaterialSize <= r.size;
                 off += kSavePhysicsMaterialSize) {
                PhysicsMaterial m;
                const uint8_t *p = r.data + off;
                const auto end = std::find(p, p + 32, uint8_t(0));
                m.name.assign(reinterpret_cast<const char *>(p), static_cast<size_t>(end - p));
                float *dst[4] = {&m.elasticity, &m.elasticityFalloff, &m.friction, &m.scatterAngle};
                for (size_t k = 0; k < 4; k++) {
                    const uint32_t bits = le32(p + 32 + 4 * k);
                    std::memcpy(dst[k], &bits, 4);
                }
                charge(total, sizeof(PhysicsMaterial) + 32, limits);
                c.inputs.materials.push_back(std::move(m));
            }
        }
        c.inputs.tableFields.push_back(field(r, total, limits));
    }
    if (!ended) {
        fail(Code::malformed);
    }
}

inline void checkCancel(const Options &opt)
{
    if (opt.cancel != nullptr && opt.cancel->load()) {
        fail(Code::cancelled);
    }
}

inline auto readContents(Source &src, const Options &opt, uint64_t &total) -> Contents
{
    Contents c;
    Cfb cfb(src, opt, total);
    cfb.open();
    const uint32_t game = cfb.find(0, "GameStg");
    if (game == kNoStream || cfb.entries()[game].type != 1) {
        fail(Code::malformed);
    }
    // GameStg's streams by name, ignoring ASCII case as compound files name them; two that differ
    // only in case are malformed.
    std::map<std::string, uint32_t> byName;
    for (const uint32_t i : cfb.children(game)) {
        if (!byName.emplace(lowerAscii(cfb.entries()[i].name), i).second) {
            fail(Code::malformed);
        }
    }
    auto stream = [&](const std::string &name) -> uint32_t {
        const auto it = byName.find(lowerAscii(name));
        return it == byName.end() || cfb.entries()[it->second].type != 2 ? kNoStream : it->second;
    };
    const uint32_t gameData = stream("GameData"), version = stream("Version");
    if (gameData == kNoStream) {
        fail(Code::malformed);
    }
    // Files before version 31 write BIFF records without lengths; they cannot be walked safely.
    if (version != kNoStream) {
        checkCancel(opt);
        const auto v = cfb.readStream(version);
        if (v.size() < 4 || int32_t(le32(v.data())) <= 30) {
            fail(Code::malformed);
        }
    }
    checkCancel(opt);
    readGameData(cfb.readStream(gameData), c, opt.limits, total);
    // As VPX loads them: GameItem0 to SEDT-1 and Collection0 to SCOL-1, a missing one skipped
    // (pintable.cpp:1550-1600, 1663-1675). Other streams are not read.
    const int64_t itemCount = (std::max)(int64_t(0), int32Value(lastField(c.inputs.tableFields, "SEDT")));
    const int64_t collectionCount =
            (std::max)(int64_t(0), int32Value(lastField(c.inputs.tableFields, "SCOL")));
    if (uint64_t(itemCount) + uint64_t(collectionCount) > opt.limits.maxStreams) {
        fail(Code::signature_too_large);
    }
    for (int64_t i = 0; i < itemCount; i++) {
        checkCancel(opt);
        if (const uint32_t e = stream("GameItem" + std::to_string(i)); e != kNoStream) {
            c.inputs.items.push_back(readItem(cfb.readStream(e), opt.limits, total));
        }
    }
    for (int64_t i = 0; i < collectionCount; i++) {
        checkCancel(opt);
        if (const uint32_t e = stream("Collection" + std::to_string(i)); e != kNoStream) {
            c.inputs.collections.push_back(readCollection(cfb.readStream(e), opt.limits, total));
        }
    }
    c.code = Code::ok;
    return c;
}

// ---------------------------------------------------------------- scripts on disk

inline auto toPath(const std::string &utf8) -> std::filesystem::path
{
#ifdef _WIN32
    return std::filesystem::path(FileSource::widen(utf8));
#else
    return std::filesystem::path(utf8);
#endif
}

/// A path from a caller's UTF-8 string; one that does not convert is unreadable rather than empty,
/// which would mean the working directory.
inline auto checkedPath(const std::string &utf8) -> std::filesystem::path
{
    std::filesystem::path p = toPath(utf8);
    if (p.empty() && !utf8.empty()) {
        fail(Code::table_unreadable);
    }
    return p;
}

inline auto fileName(const std::filesystem::path &p) -> std::string
{
    const auto s = p.filename().u8string(); // std::string in C++17, std::u8string in C++20
    return std::string(s.begin(), s.end());
}

/// Directory lookups for the script search. Each folder is listed once, and only files and folders
/// match, never a FIFO or device, and never a file that is itself a link. Under the table's folder
/// a linked folder or junction never matches either, so a table cannot lead the reader outside it.
/// In VPX's own folders a linked folder is followed, as VPX installs often link them.
class Search
{
public:
    explicit Search(const Options &opt)
        : m_opt(opt)
    {
    }

    /// The entry of dir named name ignoring ASCII case; an exact match wins, then the
    /// bytewise-smallest name, so the choice does not depend on directory order.
    auto find(const std::filesystem::path &dir, const std::string &name, bool wantDirectory,
              bool followLinks) -> std::filesystem::path
    {
        const Listing &listing = list(dir, followLinks);
        const auto it = listing.find(lowerAscii(name));
        if (it == listing.end()) {
            return {};
        }
        const Entry *best = nullptr;
        for (const Entry &e : it->second) {
            if (e.directory != wantDirectory
                || (best != nullptr && (best->name == name || (e.name != name && e.name > best->name)))) {
                continue;
            }
            best = &e;
        }
        return best == nullptr ? std::filesystem::path() : best->path;
    }

private:
    struct Entry {
        std::string name;
        std::filesystem::path path;
        bool directory;
    };
    using Listing = std::map<std::string, std::vector<Entry>>;

    auto list(const std::filesystem::path &dir, bool followLinks) -> const Listing &
    {
        const auto key = std::make_pair(dir, followLinks);
        const auto cached = m_listings.find(key);
        if (cached != m_listings.end()) {
            return cached->second;
        }
        Listing listing;
        std::error_code ec;
        std::filesystem::directory_iterator it(dir, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (++m_listed > m_opt.limits.maxListedEntries) {
                fail(Code::signature_too_large);
            }
            if ((m_listed & 1023) == 0) {
                checkCancel(m_opt);
            }
            std::error_code sec;
            const auto own = it->symlink_status(sec);
            if (sec) {
                continue;
            }
            // A linked folder counts only where links are followed; a linked file never does.
            bool directory = std::filesystem::is_directory(own);
            if (followLinks && std::filesystem::is_symlink(own)) {
                std::error_code tec;
                directory = std::filesystem::is_directory(it->status(tec)) && !tec;
                if (!directory) {
                    continue;
                }
            }
            if (!directory && !std::filesystem::is_regular_file(own)) {
                continue;
            }
#ifdef _WIN32
            // Some standard libraries (MinGW's) report a junction as a plain folder.
            if (!followLinks) {
                const DWORD attributes = GetFileAttributesW(it->path().c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES
                    || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                    continue;
                }
            }
#endif
            std::string name;
            try {
                name = fileName(it->path());
            } catch (const std::exception &) {
                continue; // a name with no UTF-8 form cannot be what a script names
            }
            listing[lowerAscii(name)].push_back({name, it->path(), directory});
        }
        // A folder that is not there is empty; one that cannot be listed is not.
        if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory) {
            fail(ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted
                         ? Code::table_unreadable_permission
                         : Code::table_unreadable);
        }
        return m_listings.emplace(key, std::move(listing)).first->second;
    }

    const Options &m_opt;
    std::map<std::pair<std::filesystem::path, bool>, Listing> m_listings;
    uint64_t m_listed = 0;
};

/// Reads a script the search found, refusing a link as the file itself.
inline auto readWholeFile(const std::filesystem::path &p, uint64_t &total, const Limits &limits)
        -> std::string
{
    FileSource f;
#ifdef _WIN32
    const auto u8 = p.u8string();
    const Code c = f.open(std::string(u8.begin(), u8.end()), true);
#else
    const Code c = f.open(p.string(), true);
#endif
    if (c != Code::ok) {
        fail(c);
    }
    if (f.size() > limits.maxStreamSize) {
        fail(Code::signature_too_large);
    }
    charge(total, f.size(), limits);
    std::string out(static_cast<size_t>(f.size()), '\0');
    if (!out.empty() && !f.read(0, reinterpret_cast<uint8_t *>(&out[0]), out.size())) {
        fail(Code::table_unreadable);
    }
    return out;
}

constexpr size_t kMaxScriptParts = 8; ///< folders and file name in one helper name

/// A script name as GetTextFile takes it ('/' or '\' between folders), split into its parts.
/// Empty when it names nothing, has more than kMaxScriptParts parts, or could leave the searched
/// folders (a root, a drive, a ".."); VPX would follow those, the reader deliberately does not.
inline auto scriptParts(const std::string &name) -> std::vector<std::string>
{
    const auto sep = [](char ch) { return ch == '/' || ch == '\\'; };
    if (name.empty() || sep(name.front()) || sep(name.back())
        || name.find_first_of(std::string(":\0", 2)) != std::string::npos) {
        return {};
    }
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < name.size()) {
        size_t end = start;
        while (end < name.size() && !sep(name[end])) {
            end++;
        }
        std::string part = name.substr(start, end - start);
        if (part == "..") {
            return {};
        }
        if (!part.empty() && part != ".") {
            if (parts.size() == kMaxScriptParts) {
                return {};
            }
            parts.push_back(std::move(part));
        }
        start = end + 1;
    }
    return parts;
}

/// base/parts..., each part matched ignoring ASCII case; empty if any part is missing.
inline auto findPath(Search &search, const std::filesystem::path &base,
                     const std::vector<std::string> &parts, bool followLinks) -> std::filesystem::path
{
    if (base.empty() || parts.empty()) {
        return {};
    }
    std::filesystem::path p = base;
    for (size_t i = 0; i < parts.size() && !p.empty(); i++) {
        p = search.find(p, parts[i], i + 1 < parts.size(), followLinks);
    }
    return p;
}

struct Found {
    std::filesystem::path path; ///< empty: not found
    bool inTable = false;       ///< found under the table's folder
};

/// VPX's FileLocator::SearchScript: the first match in its folder order.
inline auto searchScript(Search &search, const std::filesystem::path &table,
                         const std::filesystem::path &app, const std::filesystem::path &pref,
                         const std::vector<std::string> &parts) -> Found
{
    struct Root {
        const std::filesystem::path &dir;
        std::vector<const char *> subs;
    };
    const Root roots[] = {{table, {"user", "scripts"}},
                          {app, {"user", "scripts", "tables"}},
                          {pref, {"user", "scripts"}}};
    // A match in VPX's folders that lies under the table's folder (a table kept in tables/) was
    // reached through a link the table's own search refused; it is not taken either.
    std::error_code aec;
    const auto tableAbs = std::filesystem::absolute(table, aec).lexically_normal();
    auto underTable = [&](const std::filesystem::path &p) {
        std::error_code pec;
        const auto rel = std::filesystem::absolute(p, pec).lexically_normal().lexically_relative(tableAbs);
        return !aec && !pec && !rel.empty() && *rel.begin() != "..";
    };
    for (const Root &root : roots) {
        const bool inTable = &root.dir == &table;
        auto accept = [&](const std::filesystem::path &p) { return !p.empty() && (inTable || !underTable(p)); };
        if (auto p = findPath(search, root.dir, parts, !inTable); accept(p)) {
            return {p, inTable};
        }
        for (const char *sub : root.subs) {
            std::vector<std::string> sp {sub};
            sp.insert(sp.end(), parts.begin(), parts.end());
            if (auto p = findPath(search, root.dir, sp, !inTable); accept(p)) {
                return {p, inTable};
            }
        }
    }
    return {};
}

inline auto signFile(const Paths &paths, const Options &opt) -> Result
{
    Result res;
    FileSource src;
    if (const Code c = src.open(paths.table); c != Code::ok) {
        fail(c);
    }
    uint64_t total = 0;
    Contents contents = readContents(src, opt, total);
    res.inputs = std::move(contents.inputs);

    const std::filesystem::path table = checkedPath(paths.table);
    std::filesystem::path dir =
            paths.tableDirectory.empty() ? table.parent_path() : checkedPath(paths.tableDirectory);
    if (dir.empty()) {
        dir = "."; // a bare file name: the table is in the working directory
    }
    const std::filesystem::path app = checkedPath(paths.vpxDirectory);
    const std::filesystem::path pref = checkedPath(paths.prefDirectory);
    Search search(opt);

    // The script, as VPX: <table>.vbs found by the script search, else <folder>.vbs in the table's
    // folder (pintable.cpp), else the embedded CODE record.
    std::string stem = fileName(table);
    if (const size_t dot = stem.rfind('.'); dot != std::string::npos && dot > 0) {
        stem.resize(dot);
    }
    checkCancel(opt);
    std::filesystem::path sidecar = searchScript(search, dir, app, pref, {stem + ".vbs"}).path;
    if (sidecar.empty()) {
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(dir, ec).lexically_normal();
        if (!ec && abs.filename().empty()) {
            abs = abs.parent_path();
        }
        if (const std::string folder = ec ? std::string() : fileName(abs); !folder.empty()) {
            sidecar = search.find(dir, folder + ".vbs", false, false);
        }
    }
    if (!sidecar.empty()) {
        res.inputs.script = readWholeFile(sidecar, total, opt.limits);
        res.sidecar = true;
    } else if (contents.scriptProtected) {
        fail(Code::legacy_protected);
    }

    // Helpers by literal name, breadth-first, each once, through the same search: under the
    // table's folder, content hashed and its own loads followed; in VPX's folders, name only.
    // Every script is tokenized here and again by compute(), so its size and tokens are capped.
    uint64_t scriptBytes = 0, scriptTokens = 0;
    auto references = [&](const std::string &text) {
        if (text.size() > opt.limits.maxScriptBytes - (std::min)(scriptBytes, opt.limits.maxScriptBytes)) {
            fail(Code::signature_too_large);
        }
        scriptBytes += text.size();
        checkCancel(opt);
        const uint64_t budget =
                opt.limits.maxScriptTokens - (std::min)(scriptTokens, opt.limits.maxScriptTokens);
        bool capped = false;
        const auto tokens = tokenize(decodeScript(text),
                                     static_cast<size_t>((std::min)(budget, uint64_t(SIZE_MAX))), capped);
        if (capped || tokens.size() > budget) {
            fail(Code::signature_too_large);
        }
        scriptTokens += tokens.size();
        checkCancel(opt);
        auto names = scriptReferences(tokens).names;
        checkCancel(opt);
        return names;
    };
    std::vector<std::string> queue = references(res.inputs.script);
    std::unordered_set<std::string> seen;
    for (size_t q = 0; q < queue.size(); q++) {
        const std::vector<std::string> parts = scriptParts(queue[q]);
        std::string name;
        for (const auto &part : parts) {
            name += (name.empty() ? "" : "/") + part;
        }
        name = lowerAscii(parts.empty() ? queue[q] : name);
        if (seen.count(name) != 0) {
            continue;
        }
        if (seen.size() >= opt.limits.maxHelperScripts) {
            fail(Code::signature_too_large);
        }
        seen.insert(name);
        checkCancel(opt);
        const Found found = parts.empty() ? Found {} : searchScript(search, dir, app, pref, parts);
        if (found.path.empty()) {
            res.missingHelpers.push_back(name);
        } else if (found.inTable) {
            HelperScript h {name, readWholeFile(found.path, total, opt.limits)};
            for (auto &n : references(h.content)) {
                queue.push_back(std::move(n));
            }
            res.inputs.helperScripts.push_back(std::move(h));
        } else {
            res.inputs.vpxScripts.push_back(name);
        }
    }
    checkCancel(opt);
    res.signature = compute(res.inputs);
    if (!res.signature.unavailable.empty()) {
        res.code = Code::unnamed_helper;
    } else if (!res.missingHelpers.empty()) {
        // GetTextFile fails on a file VPX cannot find, a run-time error: no signature.
        res.code = Code::missing_helper;
        res.signature = Signature {};
    } else {
        res.code = Code::ok;
    }
    return res;
}

} // namespace detail

namespace detail {

/// A caller may raise a cap past what size_t holds on a 32-bit build; sizes are cast to size_t.
inline auto clamped(const Options &o) -> Options
{
    Options c = o;
    constexpr uint64_t kMax = std::numeric_limits<size_t>::max();
    for (uint64_t *v : {&c.limits.maxFileSize, &c.limits.maxStreamSize, &c.limits.maxTotalRead,
                        &c.limits.maxMeshBytes, &c.limits.maxScriptBytes}) {
        *v = (std::min)(*v, kMax);
    }
    return c;
}

} // namespace detail

/// Parses a .vpx held in memory. No I/O: the sidecar and helper scripts are the caller's to add.
inline auto parse(const uint8_t *data, size_t size, const Options &options = {}) noexcept
        -> Contents
{
    try {
        if (data == nullptr && size != 0) {
            return Contents {};
        }
        detail::MemorySource src(data, size);
        uint64_t total = 0;
        return detail::readContents(src, detail::clamped(options), total);
    } catch (const detail::Failure &f) {
        Contents c;
        c.code = f.code;
        return c;
    } catch (const std::bad_alloc &) {
        Contents c;
        c.code = Code::signature_too_large;
        return c;
    } catch (...) {
        return Contents {};
    }
}

/// Reads a .vpx from disk, resolves its script as VPX would and computes its signature. Reads only
/// the compound file's header, FAT, directory and the streams vts1 hashes, never images or sounds.
inline auto signFile(const Paths &paths, const Options &options = {}) noexcept -> Result
{
    try {
        return detail::signFile(paths, detail::clamped(options));
    } catch (const detail::Failure &f) {
        Result r;
        r.code = f.code;
        return r;
    } catch (const std::bad_alloc &) {
        Result r;
        r.code = Code::signature_too_large;
        return r;
    } catch (...) {
        Result r; // e.g. a file system error outside the table's own bytes
        r.code = Code::table_unreadable;
        return r;
    }
}

} // namespace table_signature::vpx
