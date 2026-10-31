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

enum class Code {
    ok,
    table_unreadable_permission, ///< the table or a script it needs could not be opened:
                                 ///< EACCES/EPERM
    table_unreadable,            ///< any other open or read failure
    legacy_protected,    ///< the embedded script is encrypted by VPX's legacy table protection
    unnamed_helper,      ///< compute() refused: the script loads a helper by a non-literal name
    signature_too_large, ///< a size or count cap was reached
    malformed,           ///< not a well-formed compound file or BIFF data
    cancelled,
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

struct Paths {
    std::string table;          ///< the .vpx, UTF-8
    std::string tableDirectory; ///< where the sidecar and table-local helpers live; empty: the
                                ///< table's own folder
    std::string
            vpxDirectory; ///< VPX's install folder, searched for VPX-shipped scripts; empty: none
};

struct Result {
    Code code = Code::malformed;
    Signature signature;                     ///< valid only when code is ok
    Inputs inputs;                           ///< what was hashed (as far as it was read)
    std::vector<std::string> missingHelpers; ///< literal helper names found nowhere (not hashed)
    bool sidecar = false;                    ///< the script came from <table>.vbs
};

namespace detail {

constexpr uint32_t kMaxRegSect = 0xFFFFFFFA;
constexpr uint32_t kEndOfChain = 0xFFFFFFFE;
constexpr uint32_t kNoStream = 0xFFFFFFFF;
constexpr uint32_t kDirEntrySize = 128;
constexpr uint32_t kMiniSectorSize = 64;
constexpr uint32_t kMiniCutoff = 4096;

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

    auto open(const std::string &utf8Path) -> Code
    {
#ifdef _WIN32
        const std::wstring wide = widen(utf8Path);
        if (wide.empty() && !utf8Path.empty()) {
            return Code::table_unreadable;
        }
        m_handle = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE) {
            return GetLastError() == ERROR_ACCESS_DENIED ? Code::table_unreadable_permission
                                                         : Code::table_unreadable;
        }
        LARGE_INTEGER size;
        BY_HANDLE_FILE_INFORMATION info;
        if (!GetFileInformationByHandle(m_handle, &info)
            || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0
            || !GetFileSizeEx(m_handle, &size) || size.QuadPart < 0) {
            return Code::table_unreadable;
        }
        m_size = static_cast<uint64_t>(size.QuadPart);
#else
        int flags = O_RDONLY;
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
    if (bytes > limits.maxTotalRead - std::min(total, limits.maxTotalRead)) {
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
                    break;
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
        const uint64_t miniSectors = count(root.size, kMiniSectorSize);
        const auto sectors = chain(
                true, m_firstMiniFat,
                std::min<uint64_t>(count(miniSectors * 4, m_sectorSize), m_fatEntries), false);
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

/// Walks BIFF records: int32 length (tag + data), 4-byte tag, data. CODE in GameData is the one
/// record whose length covers only its tag; an int32 byte count and the script follow it.
class Biff
{
public:
    Biff(const std::vector<uint8_t> &bytes, size_t start, uint32_t maxRecords,
         bool codeRecord = false)
        : m_p(bytes.data())
        , m_size(bytes.size())
        , m_pos(std::min(start, bytes.size()))
        , m_max(maxRecords)
        , m_code(codeRecord)
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
};

inline auto field(const Record &r) -> Field
{
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
inline void readMesh(Item &item, const Limits &limits)
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
inline auto readItem(const std::vector<uint8_t> &bytes, const Limits &limits) -> Item
{
    if (bytes.size() < 4) {
        fail(Code::malformed);
    }
    Item item;
    item.type = le32(bytes.data());
    Biff biff(bytes, 4, limits.maxRecords);
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
                point.push_back(field(r));
            }
        } else if (r.tag == "ENDB") {
            ended = true;
        } else if (r.tag == "DPNT") {
            if (item.dragPoints.size() >= limits.maxDragPoints) {
                fail(Code::signature_too_large);
            }
            inPoint = true;
        } else {
            if (r.tag == "NAME") {
                item.name = wideString(r);
            }
            item.fields.push_back(field(r));
        }
    }
    if (inPoint) {
        fail(Code::malformed);
    }
    if (item.type == kPrimitive) {
        readMesh(item, limits);
    }
    return item;
}

inline auto readCollection(const std::vector<uint8_t> &bytes, const Limits &limits) -> Collection
{
    Collection c;
    Biff biff(bytes, 0, limits.maxRecords);
    Record r;
    while (biff.next(r) && r.tag != "ENDB") {
        if (r.tag == "NAME") {
            c.name = wideString(r);
        } else if (r.tag == "ITEM") {
            c.members.push_back(wideString(r));
        } else {
            c.fields.push_back(field(r));
        }
    }
    return c;
}

// VPX's legacy table protection (ProtectionData in the SECB record): int32 fileversion, int32
// size, 24-byte paraphrase, uint32 flags. With script editing disabled the CODE record is
// encrypted.
constexpr uint32_t kDisableScriptEditing = 0x00000002;
constexpr uint32_t kDisableEverything = 0x80000000;
constexpr size_t kSavePhysicsMaterialSize = 48; // char name[32], 4 floats

inline void readGameData(const std::vector<uint8_t> &bytes, Contents &c, const Limits &limits)
{
    Biff biff(bytes, 0, limits.maxRecords, true);
    Record r;
    while (biff.next(r) && r.tag != "ENDB") {
        if (r.tag == "CODE") {
            c.inputs.script.assign(reinterpret_cast<const char *>(r.data), r.size);
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
                c.inputs.materials.push_back(std::move(m));
            }
        }
        c.inputs.tableFields.push_back(field(r));
    }
}

inline void checkCancel(const Options &opt)
{
    if (opt.cancel != nullptr && opt.cancel->load()) {
        fail(Code::cancelled);
    }
}

/// "GameItem12" -> 12, else -1.
inline auto streamIndex(const std::string &name, const char *prefix) -> int64_t
{
    const size_t n = std::strlen(prefix);
    if (name.size() <= n || name.size() > n + 9
        || lowerAscii(name.substr(0, n)) != lowerAscii(prefix)) {
        return -1;
    }
    int64_t v = 0;
    for (size_t i = n; i < name.size(); i++) {
        if (name[i] < '0' || name[i] > '9') {
            return -1;
        }
        v = v * 10 + (name[i] - '0');
    }
    return v;
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
    std::vector<std::pair<int64_t, uint32_t>> items, collections;
    uint32_t gameData = kNoStream, version = kNoStream;
    for (const uint32_t i : cfb.children(game)) {
        const DirEntry &e = cfb.entries()[i];
        if (e.type != 2) {
            continue;
        }
        if (lowerAscii(e.name) == "gamedata") {
            gameData = i;
        } else if (lowerAscii(e.name) == "version") {
            version = i;
        } else if (const int64_t n = streamIndex(e.name, "GameItem"); n >= 0) {
            items.emplace_back(n, i);
        } else if (const int64_t k = streamIndex(e.name, "Collection"); k >= 0) {
            collections.emplace_back(k, i);
        }
    }
    if (gameData == kNoStream) {
        fail(Code::malformed);
    }
    if (items.size() + collections.size() > opt.limits.maxStreams) {
        fail(Code::signature_too_large);
    }
    // Files before version 31 write BIFF records without lengths; they cannot be walked safely.
    if (version != kNoStream) {
        checkCancel(opt);
        const auto v = cfb.readStream(version);
        if (v.size() >= 4 && int32_t(le32(v.data())) <= 30) {
            fail(Code::malformed);
        }
    }
    checkCancel(opt);
    readGameData(cfb.readStream(gameData), c, opt.limits);
    std::sort(items.begin(), items.end());
    std::sort(collections.begin(), collections.end());
    for (const auto &it : items) {
        checkCancel(opt);
        c.inputs.items.push_back(readItem(cfb.readStream(it.second), opt.limits));
    }
    for (const auto &it : collections) {
        checkCancel(opt);
        c.inputs.collections.push_back(readCollection(cfb.readStream(it.second), opt.limits));
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

inline auto fileName(const std::filesystem::path &p) -> std::string
{
    const auto s = p.filename().u8string(); // std::string in C++17, std::u8string in C++20
    return std::string(s.begin(), s.end());
}

/// The regular file in dir whose name matches ASCII-case-insensitively; an exact match wins, then
/// the bytewise-smallest name, so the choice does not depend on directory order.
inline auto findFile(const std::filesystem::path &dir, const std::string &name,
                     bool wantDirectory = false) -> std::filesystem::path
{
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec), end;
    std::filesystem::path best;
    std::string bestName;
    const std::string want = lowerAscii(name);
    for (; !ec && it != end; it.increment(ec)) {
        const std::string n = fileName(it->path());
        if (lowerAscii(n) != want) {
            continue;
        }
        std::error_code tec;
        const bool ok = wantDirectory ? it->is_directory(tec) : it->is_regular_file(tec);
        if (!ok || tec) {
            continue;
        }
        if (best.empty() || n == name || (bestName != name && n < bestName)) {
            best = it->path();
            bestName = n;
        }
    }
    return best;
}

inline auto readWholeFile(const std::filesystem::path &p, uint64_t &total, const Limits &limits)
        -> std::string
{
    FileSource f;
#ifdef _WIN32
    const auto u8 = p.u8string();
    const Code c = f.open(std::string(u8.begin(), u8.end()));
#else
    const Code c = f.open(p.string());
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

/// A helper name VPX would look up as a plain file name; anything with a path in it is not
/// followed.
inline auto plainName(const std::string &n) -> bool
{
    return !n.empty() && n != "." && n != ".." && n.find_first_of("/\\:") == std::string::npos
        && n.find('\0') == std::string::npos;
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

    const std::filesystem::path table = toPath(paths.table);
    const std::filesystem::path dir =
            paths.tableDirectory.empty() ? table.parent_path() : toPath(paths.tableDirectory);
    // Script search, as VPX: the sidecar <table>.vbs, else the embedded CODE record.
    std::string stem = fileName(table);
    if (const size_t dot = stem.rfind('.'); dot != std::string::npos && dot > 0) {
        stem.resize(dot);
    }
    checkCancel(opt);
    const auto sidecar = findFile(dir, stem + ".vbs");
    if (!sidecar.empty()) {
        res.inputs.script = readWholeFile(sidecar, total, opt.limits);
        res.sidecar = true;
    } else if (contents.scriptProtected) {
        fail(Code::legacy_protected);
    }

    // Helpers by literal name, breadth-first, each once: the table's folder first (content
    // hashed, and its own loads followed), then VPX's scripts folder and VPX's folder (name only).
    std::filesystem::path vpxScripts, vpxDir;
    if (!paths.vpxDirectory.empty()) {
        vpxDir = toPath(paths.vpxDirectory);
        vpxScripts = findFile(vpxDir, "scripts", true);
    }
    std::vector<std::string> queue =
            scriptReferences(tokenize(decodeScript(res.inputs.script))).names;
    std::vector<std::string> seen;
    for (size_t q = 0; q < queue.size(); q++) {
        const std::string name = lowerAscii(queue[q]);
        if (std::find(seen.begin(), seen.end(), name) != seen.end()) {
            continue;
        }
        if (seen.size() >= opt.limits.maxHelperScripts) {
            fail(Code::signature_too_large);
        }
        seen.push_back(name);
        checkCancel(opt);
        std::filesystem::path local =
                plainName(name) ? findFile(dir, name) : std::filesystem::path();
        if (!local.empty()) {
            HelperScript h {name, readWholeFile(local, total, opt.limits)};
            for (auto &n : scriptReferences(tokenize(decodeScript(h.content))).names) {
                queue.push_back(std::move(n));
            }
            res.inputs.helperScripts.push_back(std::move(h));
        } else if (plainName(name)
                   && ((!vpxScripts.empty() && !findFile(vpxScripts, name).empty())
                       || (!vpxDir.empty() && !findFile(vpxDir, name).empty()))) {
            res.inputs.vpxScripts.push_back(name);
        } else {
            res.missingHelpers.push_back(name);
        }
    }
    res.signature = compute(res.inputs);
    res.code = res.signature.unavailable.empty() ? Code::ok : Code::unnamed_helper;
    return res;
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
        return detail::readContents(src, options, total);
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
        return detail::signFile(paths, options);
    } catch (const detail::Failure &f) {
        Result r;
        r.code = f.code;
        return r;
    } catch (const std::bad_alloc &) {
        Result r;
        r.code = Code::signature_too_large;
        return r;
    } catch (...) {
        return Result {};
    }
}

} // namespace table_signature::vpx
