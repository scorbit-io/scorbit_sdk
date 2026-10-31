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

// Test-fixture support for vpx_reader.h, shared by tools/make_vpx_fixtures.cpp and the tests: a
// synthetic table model, a minimal MS-CFB writer and zlib (fixed-Huffman) compressor that turn it
// into a .vpx, and a corpus of malformed files made by mutating a valid one. Not for production.

#pragma once

#include <table_signature/inflate.h>
#include <table_signature/table_signature.h>
#include <table_signature/vpx_reader.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace vpx_fixtures {

using namespace table_signature;
using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------- byte helpers

inline auto i32(int64_t v) -> Bytes
{
    return {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
}
inline auto f32(float f) -> Bytes
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return i32(b);
}
inline auto cat(Bytes a, const Bytes &b) -> Bytes
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
inline auto vec2(float x, float y) -> Bytes
{
    return cat(f32(x), f32(y));
}
inline auto vec4(float x, float y, float z) -> Bytes
{
    return cat(cat(vec2(x, y), f32(z)), f32(0.f));
}
/// WriteString: int32 length, then the bytes.
inline auto str(const std::string &t) -> Bytes
{
    return cat(i32(int64_t(t.size())), Bytes(t.begin(), t.end()));
}
/// WriteWideString: int32 byte count, then UTF-16LE (ASCII here).
inline auto wide(const std::string &t) -> Bytes
{
    Bytes b = i32(int64_t(t.size() * 2));
    for (const char c : t) {
        b.push_back(uint8_t(c));
        b.push_back(0);
    }
    return b;
}
inline void set32(Bytes &b, size_t off, uint32_t v)
{
    if (off + 4 <= b.size()) {
        std::memcpy(&b[off], i32(v).data(), 4);
    }
}
inline void set16(Bytes &b, size_t off, uint16_t v)
{
    if (off + 2 <= b.size()) {
        b[off] = uint8_t(v);
        b[off + 1] = uint8_t(v >> 8);
    }
}
inline auto get32(const Bytes &b, size_t off) -> uint32_t
{
    return uint32_t(b[off]) | (uint32_t(b[off + 1]) << 8) | (uint32_t(b[off + 2]) << 16)
         | (uint32_t(b[off + 3]) << 24);
}

/// One BIFF record: int32 length (tag + data), tag, data.
inline auto record(const std::string &tag, const Bytes &data) -> Bytes
{
    Bytes b = i32(int64_t(data.size() + 4));
    b.insert(b.end(), tag.begin(), tag.end());
    return cat(b, data);
}

// ---------------------------------------------------------------- zlib, fixed Huffman

class BitWriter
{
public:
    void put(uint32_t bits, int n)
    {
        for (int i = 0; i < n; i++) {
            m_acc |= ((bits >> i) & 1u) << m_n;
            if (++m_n == 8) {
                out.push_back(uint8_t(m_acc));
                m_acc = 0;
                m_n = 0;
            }
        }
    }
    void huff(uint32_t code, int len)
    {
        for (int i = len - 1; i >= 0; i--) {
            put((code >> i) & 1u, 1);
        }
    }
    void flush()
    {
        if (m_n > 0) {
            out.push_back(uint8_t(m_acc));
            m_acc = 0;
            m_n = 0;
        }
    }
    Bytes out;

private:
    uint32_t m_acc = 0;
    int m_n = 0;
};

inline void literal(BitWriter &w, int v)
{
    if (v < 144) {
        w.huff(0x30 + v, 8);
    } else if (v < 256) {
        w.huff(0x190 + v - 144, 9);
    } else if (v < 280) {
        w.huff(v - 256, 7);
    } else {
        w.huff(0xC0 + v - 280, 8);
    }
}

/// zlib stream with one fixed-Huffman block and greedy LZ77 matches.
inline auto zlibCompress(const Bytes &in) -> Bytes
{
    using namespace table_signature::inflate::detail;
    BitWriter w;
    w.out = {0x78, 0x01};
    w.put(1, 1);
    w.put(1, 2);
    size_t i = 0;
    while (i < in.size()) {
        size_t bestLen = 0, bestDist = 0;
        const size_t window = std::min<size_t>(i, 4096);
        for (size_t d = 1; d <= window; d++) {
            size_t l = 0;
            while (l < 258 && i + l < in.size() && in[i + l] == in[i + l - d]) {
                l++;
            }
            if (l > bestLen) {
                bestLen = l;
                bestDist = d;
                if (l == 258) {
                    break;
                }
            }
        }
        if (bestLen < 3) {
            literal(w, in[i++]);
            continue;
        }
        int li = 28;
        while (kLengthBase[li] > bestLen) {
            li--;
        }
        literal(w, 257 + li);
        w.put(uint32_t(bestLen - kLengthBase[li]), kLengthExtra[li]);
        int di = 29;
        while (kDistBase[di] > bestDist) {
            di--;
        }
        w.huff(uint32_t(di), 5);
        w.put(uint32_t(bestDist - kDistBase[di]), kDistExtra[di]);
        i += bestLen;
    }
    literal(w, 256);
    w.flush();
    const uint32_t a = adler32(in.data(), in.size());
    for (int s = 24; s >= 0; s -= 8) {
        w.out.push_back(uint8_t(a >> s));
    }
    return w.out;
}

// ---------------------------------------------------------------- MS-CFB writer

struct Stream {
    std::string name; // a child of GameStg
    Bytes data;
};

struct Built {
    Bytes bytes;
    uint32_t fatSector = 0; // first FAT sector (the FAT is contiguous)
    uint32_t dirSector = 0; // first directory sector (contiguous)
    uint32_t miniFatSector = vpx::detail::kEndOfChain;
    std::map<std::string, uint32_t>
            entry; // stream name -> directory index; "Root Entry", "GameStg"
    std::map<std::string, uint32_t> start; // stream name -> first (mini)sector
    uint32_t entries = 0;
    uint32_t sectorSize = 512;
};

struct CfbOptions {
    bool version4 = false;        // 4096-byte sectors instead of 512
    uint32_t minFatSectors = 0;   // pad the FAT with unused sectors; above 109 this forces a DIFAT
};

inline auto sectorOffset(uint32_t sid, uint32_t sectorSize = 512) -> size_t
{
    return (size_t(sid) + 1) * sectorSize;
}

/// Streams under 4096 bytes in the mini stream, no timestamps. Version 3 (512-byte sectors) unless
/// opts.version4.
inline auto writeCfb(const std::vector<Stream> &streams, CfbOptions opts = {}) -> Built
{
    const uint32_t S = opts.version4 ? 4096 : 512;
    const uint32_t perSec = S / 4; // 32-bit entries per sector
    const uint32_t dirPerSec = S / 128;
    constexpr uint32_t kFree = 0xFFFFFFFF, kEnd = 0xFFFFFFFE, kFatSect = 0xFFFFFFFD;
    Built b;
    b.sectorSize = S;
    Bytes mini;
    std::vector<uint32_t> miniFat;
    std::vector<uint32_t> startSector(streams.size(), kEnd);
    std::vector<size_t> regular;
    for (size_t i = 0; i < streams.size(); i++) {
        const auto &s = streams[i];
        if (s.data.empty()) {
            continue;
        }
        if (s.data.size() < 4096) {
            const uint32_t first = uint32_t(mini.size() / 64);
            const uint32_t n = uint32_t((s.data.size() + 63) / 64);
            for (uint32_t k = 0; k < n; k++) {
                miniFat.push_back(k + 1 < n ? first + k + 1 : kEnd);
            }
            mini.insert(mini.end(), s.data.begin(), s.data.end());
            mini.resize(size_t(first + n) * 64, 0);
            startSector[i] = first;
        } else {
            regular.push_back(i);
        }
    }
    const uint32_t entries = uint32_t(2 + streams.size());
    const uint32_t nDir = (entries + dirPerSec - 1) / dirPerSec;
    const uint32_t nMiniFat = uint32_t((miniFat.size() + perSec - 1) / perSec);
    const uint32_t nContainer = uint32_t((mini.size() + S - 1) / S);
    uint32_t nRegular = 0;
    for (const size_t i : regular) {
        nRegular += uint32_t((streams[i].data.size() + S - 1) / S);
    }
    // The header lists 109 FAT sectors; DIFAT sectors list perSec - 1 more each.
    uint32_t nFat = std::max<uint32_t>(1, opts.minFatSectors), nDifat = 0;
    for (;;) {
        nDifat = nFat > 109 ? (nFat - 109 + perSec - 2) / (perSec - 1) : 0;
        if (nFat * perSec >= nFat + nDifat + nDir + nMiniFat + nContainer + nRegular) {
            break;
        }
        nFat++;
    }
    const uint32_t total = nFat + nDifat + nDir + nMiniFat + nContainer + nRegular;
    std::vector<uint32_t> fat(size_t(nFat) * perSec, kFree);
    uint32_t next = 0;
    auto run = [&](uint32_t n) -> uint32_t {
        const uint32_t first = next;
        for (uint32_t k = 0; k < n; k++) {
            fat[first + k] = k + 1 < n ? first + k + 1 : kEnd;
        }
        next += n;
        return n ? first : kEnd;
    };
    for (uint32_t k = 0; k < nFat; k++) {
        fat[next++] = kFatSect;
    }
    const uint32_t firstDifat = nDifat ? next : kEnd;
    for (uint32_t k = 0; k < nDifat; k++) {
        fat[next++] = 0xFFFFFFFC; // DIFSECT
    }
    b.fatSector = 0;
    b.dirSector = run(nDir);
    b.miniFatSector = run(nMiniFat);
    const uint32_t container = run(nContainer);
    for (const size_t i : regular) {
        startSector[i] = run(uint32_t((streams[i].data.size() + S - 1) / S));
    }
    b.bytes.assign(sectorOffset(total, S), 0);
    // Header
    static const uint8_t sig[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    std::memcpy(&b.bytes[0], sig, 8);
    set16(b.bytes, 0x18, 0x3E);
    set16(b.bytes, 0x1A, opts.version4 ? 4 : 3);
    set16(b.bytes, 0x1C, 0xFFFE);
    set16(b.bytes, 0x1E, opts.version4 ? 12 : 9);
    set16(b.bytes, 0x20, 6);
    if (opts.version4) {
        set32(b.bytes, 0x28, nDir);
    }
    set32(b.bytes, 0x2C, nFat);
    set32(b.bytes, 0x30, b.dirSector);
    set32(b.bytes, 0x38, 4096);
    set32(b.bytes, 0x3C, b.miniFatSector);
    set32(b.bytes, 0x40, nMiniFat);
    set32(b.bytes, 0x44, firstDifat);
    set32(b.bytes, 0x48, nDifat);
    for (uint32_t k = 0; k < 109; k++) {
        set32(b.bytes, 0x4C + 4 * k, k < nFat ? k : kFree);
    }
    for (uint32_t d = 0; d < nDifat; d++) {
        const size_t p = sectorOffset(firstDifat + d, S);
        for (uint32_t k = 0; k < perSec - 1; k++) {
            const uint32_t sid = 109 + d * (perSec - 1) + k;
            set32(b.bytes, p + 4 * k, sid < nFat ? sid : kFree);
        }
        set32(b.bytes, p + 4 * (perSec - 1), d + 1 < nDifat ? firstDifat + d + 1 : kEnd);
    }
    for (size_t k = 0; k < fat.size(); k++) {
        set32(b.bytes, sectorOffset(0, S) + 4 * k, fat[k]);
    }
    for (size_t k = 0; k < size_t(nMiniFat) * perSec; k++) {
        set32(b.bytes, sectorOffset(b.miniFatSector, S) + 4 * k,
              k < miniFat.size() ? miniFat[k] : kFree);
    }
    if (!mini.empty()) {
        std::memcpy(&b.bytes[sectorOffset(container, S)], mini.data(), mini.size());
    }
    for (const size_t i : regular) {
        std::memcpy(&b.bytes[sectorOffset(startSector[i], S)], streams[i].data.data(),
                    streams[i].data.size());
    }
    // Directory: a balanced sibling tree in CFB order (name length, then upper-case name).
    std::vector<uint32_t> order(streams.size());
    for (uint32_t i = 0; i < order.size(); i++) {
        order[i] = i;
    }
    auto upper = [](std::string s) {
        for (auto &c : s) {
            c = char(std::toupper(static_cast<unsigned char>(c)));
        }
        return s;
    };
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t c) {
        const auto &x = streams[a].name, &y = streams[c].name;
        return x.size() != y.size() ? x.size() < y.size() : upper(x) < upper(y);
    });
    std::vector<uint32_t> left(entries, kFree), right(entries, kFree);
    struct Span {
        size_t lo, hi;
    };
    auto build = [&](auto &&self, size_t lo, size_t hi) -> uint32_t {
        if (lo >= hi) {
            return kFree;
        }
        const size_t mid = (lo + hi) / 2;
        const uint32_t e = 2 + order[mid];
        left[e] = self(self, lo, mid);
        right[e] = self(self, mid + 1, hi);
        return e;
    };
    const uint32_t treeRoot = build(build, 0, order.size());
    auto writeEntry = [&](uint32_t index, const std::string &name, uint8_t type, uint32_t child,
                          uint32_t start, uint64_t size) {
        const size_t p = sectorOffset(b.dirSector, S) + size_t(index) * 128;
        for (size_t k = 0; k < name.size(); k++) {
            set16(b.bytes, p + 2 * k, uint8_t(name[k]));
        }
        set16(b.bytes, p + 64, uint16_t((name.size() + 1) * 2));
        b.bytes[p + 66] = type;
        b.bytes[p + 67] = 1; // black
        set32(b.bytes, p + 68, left[index]);
        set32(b.bytes, p + 72, right[index]);
        set32(b.bytes, p + 76, child);
        set32(b.bytes, p + 116, start);
        set32(b.bytes, p + 120, uint32_t(size));
        b.entry[name] = index;
        b.start[name] = start;
    };
    writeEntry(0, "Root Entry", 5, 1, mini.empty() ? kEnd : container, mini.size());
    writeEntry(1, "GameStg", 1, treeRoot, 0, 0);
    for (uint32_t i = 0; i < streams.size(); i++) {
        writeEntry(2 + i, streams[i].name, 2, kFree, startSector[i], streams[i].data.size());
    }
    for (uint32_t i = entries; i < nDir * dirPerSec; i++) {
        const size_t p = sectorOffset(b.dirSector, S) + size_t(i) * 128;
        set32(b.bytes, p + 68, kFree);
        set32(b.bytes, p + 72, kFree);
        set32(b.bytes, p + 76, kFree);
    }
    b.entries = entries;
    return b;
}

// ---------------------------------------------------------------- the synthetic table

/// The .vpx side of a fixture, in the shape vpx_reader yields it: inputs.script is the CODE record,
/// and every item, collection and table field lists its records in file order.
struct Table {
    Inputs inputs;
    std::vector<Stream> extra; // streams the reader must not need (an image)
};

inline auto named(uint32_t type, const std::string &name) -> Item
{
    Item it;
    it.type = type;
    it.name = name;
    it.fields.push_back({"NAME", wide(name)});
    return it;
}

inline auto dp(float x, float y) -> DragPoint
{
    return {{"VCEN", vec2(x, y)}, {"SMTH", i32(0)}, {"SLNG", i32(0)}, {"TEXC", f32(0.f)}};
}

inline auto physicsRecord(const std::vector<PhysicsMaterial> &mats) -> Bytes
{
    Bytes out;
    for (const auto &m : mats) {
        Bytes name(32, 0);
        std::memcpy(name.data(), m.name.data(), std::min<size_t>(m.name.size(), 31));
        out = cat(out, name);
        for (const float f : {m.elasticity, m.elasticityFalloff, m.friction, m.scatterAngle}) {
            out = cat(out, f32(f));
        }
    }
    return out;
}

/// A primitive's mesh: a box of 24 vertices (x, y, z, normal, uv) and 36 indices, compressed.
inline void addMesh(Item &prim, float scale)
{
    Bytes vertices, indices;
    for (int face = 0; face < 6; face++) {
        for (int corner = 0; corner < 4; corner++) {
            const float x = ((corner & 1) ? 1.f : -1.f) * scale;
            const float y = ((corner & 2) ? 1.f : -1.f) * scale;
            const float z = float(face) * 0.5f * scale;
            for (const float f : {x, y, z}) {
                prim.mesh.positions.push_back(f);
                vertices = cat(vertices, f32(f));
            }
            for (const float f : {0.f, 0.f, 1.f, float(corner & 1), float((corner >> 1) & 1)}) {
                vertices = cat(vertices, f32(f));
            }
        }
        for (const uint32_t k : {0u, 1u, 2u, 2u, 1u, 3u}) {
            const uint32_t idx = uint32_t(face) * 4 + k;
            prim.mesh.indices.push_back(idx);
            indices.push_back(uint8_t(idx));
            indices.push_back(uint8_t(idx >> 8));
        }
    }
    const Bytes cv = zlibCompress(vertices), ci = zlibCompress(indices);
    prim.fields.push_back({"U3DM", i32(1)});
    prim.fields.push_back({"M3VN", i32(24)});
    prim.fields.push_back({"M3CY", i32(int64_t(cv.size()))});
    prim.fields.push_back({"M3CX", cv});
    prim.fields.push_back({"M3FN", i32(36)});
    prim.fields.push_back({"M3CJ", i32(int64_t(ci.size()))});
    prim.fields.push_back({"M3CI", ci});
}

/// A FONT descriptor as WriteFontDescriptor lays it out after the tag: version, charset,
/// attributes, weight, size, name length, name.
inline auto fontDescriptor(const std::string &name, uint32_t size) -> Bytes
{
    Bytes b = {1, 0, 0, 0, 0x90, 0x01};
    b = cat(b, i32(size));
    b.push_back(uint8_t(name.size()));
    b.insert(b.end(), name.begin(), name.end());
    return b;
}

inline auto script() -> std::string
{
    return "Option Explicit\r\n"
           "' Scoring\r\n"
           "Dim Score : Score = 0\r\n"
           "ExecuteGlobal GetTextFile(\"core.vbs\")\r\n"
           "LoadScript \"Helper.vbs\"\r\n"
           "Sub Bumper1_Hit()\r\n"
           "    AddScore 1000\r\n"
           "End Sub\r\n"
           "Sub AddScore(points)\r\n"
           "    Score = Score + points\r\n"
           "End Sub\r\n";
}

inline auto cosmeticScript() -> std::string
{
    return "option explicit\n"
           "dim score\n"
           "score = 0   ' starts at zero\n"
           "executeglobal gettextfile(\"core.vbs\")\n"
           "loadscript \"Helper.vbs\"\n"
           "sub bumper1_hit()\n"
           "\taddscore _\n"
           "\t  1000\n"
           "end sub\n"
           "\n"
           "SUB ADDSCORE(Points)\n"
           "  Score = Score + Points ' add\n"
           "END SUB\n";
}

constexpr const char *kHelper =
        "Sub HelperBonus() : Score = Score + 1 : End Sub\r\nLoadScript \"extra.vbs\"\r\n";
constexpr const char *kExtra = "Sub ExtraRule()\r\n    Score = Score * 2\r\nEnd Sub\r\n";
constexpr const char *kSidecar =
        "Option Explicit\r\nSub Bumper1_Hit()\r\n    Score = Score + 5\r\nEnd Sub\r\n";

/// The reference table. cosmetic changes only fields vts1 does not hash; bumperY moves the bumper.
inline auto table(bool cosmetic = false, float bumperY = 600.f) -> Table
{
    Table t;
    Inputs &in = t.inputs;
    in.script = cosmetic ? cosmeticScript() : script();

    auto wall1 = named(kSurface, "Wall1");
    wall1.fields.push_back({"HTTP", f32(50.f)});
    wall1.fields.push_back({"HTBT", f32(0.f)});
    wall1.fields.push_back({"ELAS", f32(0.3f)});
    wall1.fields.push_back({"IMAG", str(cosmetic ? "wood_dark" : "wood")});
    wall1.dragPoints = {dp(100.f, 100.f), dp(200.f, 100.f), dp(200.f, 200.f), dp(100.f, 200.f)};
    auto wall2 = named(kSurface, "Wall2");
    wall2.fields.push_back({"HTTP", f32(25.f)});
    wall2.dragPoints = {dp(300.f, 300.f), dp(400.f, 300.f), dp(350.f, 400.f)};
    auto flipper = named(kFlipper, "LeftFlipper");
    for (const auto &f : std::vector<Field> {{"VCEN", vec2(280.f, 1700.f)},
                                             {"BASR", f32(21.f)},
                                             {"ENDR", f32(13.f)},
                                             {"FLPR", f32(130.f)},
                                             {"STRG", f32(2200.f)},
                                             {"ELAS", f32(0.8f)},
                                             {"FRIC", f32(0.9f)},
                                             {"SURF", str("Wall1")},
                                             {"IMAG", str(cosmetic ? "flipper_red" : "flipper")}}) {
        flipper.fields.push_back(f);
    }
    auto bumper = named(kBumper, "Bumper1");
    for (const auto &f : std::vector<Field> {{"VCEN", vec2(450.f, bumperY)},
                                             {"RADI", f32(45.f)},
                                             {"FORC", f32(15.f)},
                                             {"THRS", f32(1.f)},
                                             {"SURF", str("")},
                                             {"BAMA", str(cosmetic ? "Gold" : "Silver")}}) {
        bumper.fields.push_back(f);
    }
    auto prim = named(kPrimitive, "Prim1");
    prim.fields.push_back({"VPOS", vec4(500.f, 900.f, 0.f)});
    prim.fields.push_back({"VSIZ", vec4(100.f, 100.f, 100.f)});
    prim.fields.push_back({"CLDR", i32(1)});
    addMesh(prim, 10.f);
    auto light = named(kLight, "Light1");
    light.fields.push_back({"STAT", i32(1)});
    light.fields.push_back({"COLR", i32(cosmetic ? 0x00FF00 : 0xFF0000)});
    // A textbox and a decal: their FONT record has untagged data after it, as in real tables.
    auto textbox = named(vpx::detail::kTextbox, "Score1");
    textbox.fields.push_back({"VER1", vec2(10.f, 10.f)});
    textbox.fields.push_back({"FONT", fontDescriptor(cosmetic ? "Courier New" : "Arial", 120000)});
    textbox.fields.push_back({"TEXT", str("0")});
    auto decal = named(vpx::detail::kDecal, "Logo");
    decal.fields.push_back({"VCEN", vec2(500.f, 1500.f)});
    decal.fields.push_back({"FONT", fontDescriptor("Tahoma", 80000)});
    decal.fields.push_back({"TYPE", i32(0)});
    in.items = {wall1, wall2, flipper, bumper, prim, light, textbox, decal};

    Collection targets;
    targets.name = "Targets";
    targets.fields = {{"EVNT", i32(1)}, {"SSNG", i32(0)}};
    targets.members = {"Bumper1", "LeftFlipper"};
    in.collections = {targets};

    in.materials = {{"Rubber", 0.8f, 0.3f, 0.6f, 5.f}, {"Metal", 0.2f, 0.f, 0.15f, 0.f}};
    in.tableFields = {{"NAME", wide(cosmetic ? "Renamed" : "Table")},
                      // VPX loads GameItem0..SEDT-1 and Collection0..SCOL-1.
                      {"SEDT", i32(int64_t(in.items.size()))},
                      {"SCOL", i32(int64_t(in.collections.size()))},
                      {"SLOP", f32(6.f)},
                      {"SLPX", f32(6.f)},
                      {"GAVT", f32(1.762985f)},
                      {"FRCT", f32(0.075f)},
                      {"ELAS", f32(0.25f)},
                      {"PHMA", physicsRecord(in.materials)},
                      // An unknown record the reader skips by length; large enough for a regular
                      // sector chain.
                      {"ZZZZ", Bytes(5000, cosmetic ? 0x5A : 0xA5)}};
    t.extra.push_back({"Image0", Bytes(6000, 0x11)});
    return t;
}

/// Sets SEDT and SCOL to the model's item and collection counts, after a change to either.
inline void setCounts(Inputs &in)
{
    for (auto &f : in.tableFields) {
        if (f.tag == "SEDT") {
            f.value = i32(int64_t(in.items.size()));
        } else if (f.tag == "SCOL") {
            f.value = i32(int64_t(in.collections.size()));
        }
    }
}

inline auto itemStream(const Item &it) -> Bytes
{
    Bytes b = i32(it.type);
    for (const auto &f : it.fields) {
        // FONT: a tag-only record, then the descriptor untagged (WriteFontDescriptor).
        b = f.tag == "FONT" ? cat(cat(b, record(f.tag, {})), f.value) : cat(b, record(f.tag, f.value));
    }
    for (const auto &p : it.dragPoints) {
        b = cat(b, record("DPNT", {}));
        for (const auto &f : p) {
            b = cat(b, record(f.tag, f.value));
        }
        b = cat(b, record("ENDB", {}));
    }
    return cat(b, record("ENDB", {}));
}

inline auto collectionStream(const Collection &c) -> Bytes
{
    Bytes b = record("NAME", wide(c.name));
    for (const auto &f : c.fields) {
        b = cat(b, record(f.tag, f.value));
    }
    for (const auto &m : c.members) {
        b = cat(b, record("ITEM", wide(m)));
    }
    return cat(b, record("ENDB", {}));
}

/// GameData: CODE first (a tag-only record, then int32 count and the script), then the fields.
inline auto gameDataStream(const Inputs &in) -> Bytes
{
    Bytes b = cat(record("CODE", {}), str(in.script));
    for (const auto &f : in.tableFields) {
        b = cat(b, record(f.tag, f.value));
    }
    return cat(b, record("ENDB", {}));
}

inline auto streams(const Table &t) -> std::vector<Stream>
{
    std::vector<Stream> s = {{"Version", i32(1080)}, {"GameData", gameDataStream(t.inputs)}};
    for (size_t i = 0; i < t.inputs.items.size(); i++) {
        s.push_back({"GameItem" + std::to_string(i), itemStream(t.inputs.items[i])});
    }
    for (size_t i = 0; i < t.inputs.collections.size(); i++) {
        s.push_back({"Collection" + std::to_string(i), collectionStream(t.inputs.collections[i])});
    }
    s.insert(s.end(), t.extra.begin(), t.extra.end());
    return s;
}

inline auto build(const Table &t) -> Built
{
    return writeCfb(streams(t));
}

/// The legacy protection record with script editing disabled (VPX's ProtectionData layout).
inline auto protectionRecord() -> Field
{
    Bytes p = cat(i32(1), i32(48));
    p.resize(32, 0);
    return {"SECB", cat(cat(p, i32(0x2)), Bytes(12, 0))};
}

// ---------------------------------------------------------------- malformed corpus

struct Case {
    std::string name;
    Bytes bytes;
    /// The code the reader must return, or -1 when any code is acceptable (only no crash/hang).
    int expect = -1;
};

inline auto withStream(const Table &t, const std::string &name, const Bytes &data) -> Bytes
{
    auto s = streams(t);
    for (auto &st : s) {
        if (st.name == name) {
            st.data = data;
        }
    }
    return writeCfb(s).bytes;
}

/// Malformed inputs, all derived from the valid reference table.
inline auto corpus() -> std::vector<Case>
{
    using vpx::Code;
    constexpr int kMalformed = int(Code::malformed), kTooLarge = int(Code::signature_too_large);
    const Table t = table();
    const Built valid = build(t);
    const Bytes &v = valid.bytes;
    std::vector<Case> out;
    auto mutate = [&](const std::string &name, size_t off, uint32_t value, int width, int expect) {
        Bytes b = v;
        if (width == 2) {
            set16(b, off, uint16_t(value));
        } else if (width == 1) {
            b[off] = uint8_t(value);
        } else {
            set32(b, off, value);
        }
        out.push_back({name, b, expect});
    };
    const uint32_t fileSectors = uint32_t(v.size() / 512 - 1);

    // Header
    for (const uint32_t shift : {0u, 3u, 8u, 10u, 12u, 15u, 31u, 32u, 0xFFFFu}) {
        mutate("sector shift " + std::to_string(shift), 0x1E, shift, 2, kMalformed);
    }
    for (const uint32_t shift : {0u, 5u, 31u, 0xFFFFu}) {
        mutate("mini sector shift " + std::to_string(shift), 0x20, shift, 2, kMalformed);
    }
    mutate("major version 4 with 512-byte sectors", 0x1A, 4, 2, kMalformed);
    mutate("byte order mark", 0x1C, 0xFEFF, 2, kMalformed);
    mutate("signature", 0, 0, 1, kMalformed);
    for (const uint32_t n : {0u, 2u, 1000u, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
        mutate("FAT sector count " + std::to_string(n), 0x2C, n, 4, n == 2 ? -1 : kMalformed);
    }
    for (const uint32_t cutoff : {0u, 64u, 4095u, 4097u, 0xFFFFFFFFu}) {
        mutate("mini stream cutoff " + std::to_string(cutoff), 0x38, cutoff, 4, kMalformed);
    }
    for (const uint32_t sid :
         {0xFFFFFFFEu, 0xFFFFFFFAu, 0x7FFFFFFFu, fileSectors, fileSectors + 100, 0u}) {
        mutate("first directory sector " + std::to_string(sid), 0x30, sid, 4,
               sid == 0 ? -1 : kMalformed);
    }
    for (const uint32_t sid : {0xFFFFFFFEu, 0x7FFFFFFFu, fileSectors + 1, valid.dirSector}) {
        mutate("first mini FAT sector " + std::to_string(sid), 0x3C, sid, 4, -1);
    }
    // The mini FAT sector count: fewer than the mini stream needs, or more than the chain holds.
    for (const uint32_t n : {0u, 2u, 1000u, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
        mutate("mini FAT sector count " + std::to_string(n), 0x40, n, 4, kMalformed);
    }
    for (const uint32_t sid : {0xFFFFFFFFu, 0x7FFFFFFFu, fileSectors + 1}) {
        mutate("DIFAT[0] " + std::to_string(sid), 0x4C, sid, 4, kMalformed);
    }
    {
        // More FAT sectors than the header holds, through a DIFAT chain that loops on itself.
        Bytes b = v;
        set32(b, 0x2C, 110);
        set32(b, 0x44, valid.dirSector);
        set32(b, 0x48, 0xFFFFFFFF);
        out.push_back({"DIFAT chain", b, kMalformed});
    }

    // FAT
    const size_t fat = sectorOffset(valid.fatSector);
    mutate("directory chain cycle", fat + 4 * valid.dirSector, valid.dirSector, 4, kMalformed);
    mutate("mini FAT chain cycle", fat + 4 * valid.miniFatSector, valid.miniFatSector, 4, kMalformed);
    const uint32_t gameData = valid.start.at("GameData");
    mutate("stream chain cycle", fat + 4 * gameData, gameData, 4, kMalformed);
    mutate("stream chain past the file", fat + 4 * gameData, fileSectors + 10, 4, kMalformed);
    mutate("stream chain out of range", fat + 4 * gameData, 0x7FFFFFF0, 4, kMalformed);
    mutate("stream chain ends early", fat + 4 * gameData, 0xFFFFFFFE, 4, kMalformed);
    mutate("stream chain into free", fat + 4 * gameData, 0xFFFFFFFF, 4, kMalformed);
    const uint32_t container = get32(v, sectorOffset(valid.dirSector) + 116);
    mutate("mini container chain cycle", fat + 4 * container, container, 4, -1);
    for (uint32_t sid = 0; sid < 128 && sid < fileSectors; sid++) {
        mutate("FAT entry " + std::to_string(sid) + " to itself", fat + 4 * sid, sid, 4, -1);
    }

    // Directory
    const size_t dir = sectorOffset(valid.dirSector);
    for (const auto &e : valid.entry) {
        const size_t p = dir + size_t(e.second) * 128;
        const std::string n = e.first;
        mutate(n + " name length 66", p + 64, 66, 2, kMalformed);
        mutate(n + " name length 65535", p + 64, 0xFFFF, 2, kMalformed);
        mutate(n + " name length odd", p + 64, 3, 2, kMalformed);
        mutate(n + " type 3", p + 66, 3, 1, kMalformed);
        mutate(n + " type 0", p + 66, 0, 1, kMalformed);
        mutate(n + " child to itself", p + 76, e.second, 4, -1);
        mutate(n + " left to itself", p + 68, e.second, 4, -1);
        mutate(n + " right to root", p + 72, 0, 4, -1);
        mutate(n + " right out of range", p + 72, 0x7FFFFFFF, 4, -1);
        mutate(n + " right past the entries", p + 72, valid.entries + 1, 4, -1);
        for (const uint32_t sid : {0x7FFFFFFFu, 0xFFFFFFFAu, fileSectors + 1}) {
            mutate(n + " start " + std::to_string(sid), p + 116, sid, 4, -1);
        }
        for (const uint32_t size : {0u, 63u, 4095u, 4096u, 4097u, 0x7FFFFFFFu, 0xFFFFFFFFu}) {
            mutate(n + " size " + std::to_string(size), p + 120, size, 4, -1);
        }
    }

    // BIFF
    const Item &wall = t.inputs.items[0];
    const Bytes wallBytes = itemStream(wall);
    auto biff = [&](const std::string &name, const std::string &stream, const Bytes &data,
                    int expect) {
        out.push_back({name, withStream(t, stream, data), expect});
    };
    {
        Bytes b = wallBytes;
        set32(b, 4, 0x1000); // first record runs past the stream
        biff("record length past the stream", "GameItem0", b, kMalformed);
        set32(b, 4, 0x7FFFFFFF);
        biff("record length 2^31-1", "GameItem0", b, kMalformed);
        set32(b, 4, 0x80000000);
        biff("record length negative", "GameItem0", b, kMalformed);
        set32(b, 4, 3);
        biff("record length 3", "GameItem0", b, kMalformed);
        biff("item stream of 3 bytes", "GameItem0", Bytes(3, 0), kMalformed);
        biff("item stream with a partial record", "GameItem0", cat(i32(0), Bytes(5, 0)),
             kMalformed);
        Bytes name = cat(i32(0), record("NAME", cat(i32(0x1000), Bytes(4, 0))));
        biff("wide string past its record", "GameItem0", name, kMalformed);
        Bytes odd = cat(i32(0), record("NAME", cat(i32(3), Bytes(4, 0))));
        biff("wide string odd length", "GameItem0", odd, kMalformed);
    }
    {
        auto repeat = [](const Bytes &unit, size_t n) {
            Bytes b = i32(0);
            b.reserve(4 + unit.size() * n);
            for (size_t k = 0; k < n; k++) {
                b.insert(b.end(), unit.begin(), unit.end());
            }
            return b;
        };
        const Bytes bomb = repeat(record("DPNT", {}), 200000);
        biff("drag point nesting bomb", "GameItem0", bomb, kMalformed);
        const Bytes many = repeat(cat(record("DPNT", {}), record("ENDB", {})), 70000);
        biff("drag point count bomb", "GameItem0", many, kTooLarge);
        const Bytes records = repeat(record("ZZZZ", {}), 1100000);
        biff("record count bomb", "GameItem0", records, kTooLarge);
    }
    {
        Bytes code = cat(record("CODE", {}), i32(0x10000));
        biff("CODE count past the stream", "GameData", code, kMalformed);
        biff("CODE count negative", "GameData", cat(record("CODE", {}), i32(-1)), kMalformed);
        biff("CODE without its count", "GameData", record("CODE", {}), kMalformed);
        biff("version 30", "Version", i32(30), kMalformed);
    }
    {
        // A textbox's FONT descriptor cut short, its name running past the stream, or missing.
        const Bytes head = cat(i32(vpx::detail::kTextbox), record("FONT", {}));
        biff("FONT descriptor cut short", "GameItem6", cat(head, Bytes(10, 0)), kMalformed);
        Bytes longName = cat(head, fontDescriptor("Arial", 1));
        longName[longName.size() - 6] = 0xFF; // the name length byte
        biff("FONT name past the stream", "GameItem6", longName, kMalformed);
        biff("FONT without its descriptor", "GameItem6", head, kMalformed);
        // In an item type that never writes FONT, a tag-only FONT is an empty record.
        biff("tag-only FONT in a wall", "GameItem0",
             cat(cat(i32(kSurface), record("FONT", {})), record("ENDB", {})), int(Code::ok));
    }
    {
        // Primitive meshes: an inflate bomb, a vertex count past the cap, corrupt and truncated
        // data.
        Item prim = t.inputs.items[4];
        auto withPrim = [&](const std::string &name, const std::string &tag, const Bytes &value,
                            int expect) {
            Item p = prim;
            p.fields.push_back({tag, value});
            biff(name, "GameItem4", itemStream(p), expect);
        };
        withPrim("inflate bomb", "M3CX", zlibCompress(Bytes(1u << 20, 0)), kMalformed);
        withPrim("vertex count past the cap", "M3VN", i32(4000000), kTooLarge);
        withPrim("vertex count negative", "M3VN", i32(-5), -1);
        withPrim("corrupt compressed vertices", "M3CX", Bytes(64, 0x55), kMalformed);
        Bytes cut = zlibCompress(Bytes(768, 1));
        cut.resize(cut.size() / 2);
        withPrim("truncated compressed vertices", "M3CX", cut, kMalformed);
        withPrim("raw indices of the wrong size", "M3DI", Bytes(10, 0), -1);
    }
    {
        // Streams either side of the mini stream cutoff, valid, then with sizes swapped across it.
        for (const size_t size : {4095u, 4096u, 4097u}) {
            Item it = wall;
            Bytes s = itemStream(it);
            const size_t pad = size - s.size() - 8; // one filler record before the final ENDB
            const Bytes filler = record("ZZZZ", Bytes(pad, 0x20));
            s.insert(s.end() - 8, filler.begin(), filler.end());
            Bytes good = withStream(t, "GameItem0", s);
            out.push_back(
                    {"item stream of " + std::to_string(size) + " bytes", good, int(Code::ok)});
            const Built b = writeCfb([&] {
                auto st = streams(t);
                st[2].data = s;
                return st;
            }());
            Bytes swapped = b.bytes;
            set32(swapped, sectorOffset(b.dirSector) + size_t(b.entry.at("GameItem0")) * 128 + 120,
                  size < 4096 ? 4096 : 4095);
            out.push_back(
                    {"item stream of " + std::to_string(size) + " bytes, size across the cutoff",
                     swapped, -1});
        }
    }

    // Truncation at every 64 bytes, and deterministic byte flips.
    for (size_t n = 0; n < v.size(); n += 64) {
        out.push_back(
                {"truncated to " + std::to_string(n), Bytes(v.begin(), v.begin() + long(n)), -1});
    }
    out.push_back({"truncated by one byte", Bytes(v.begin(), v.end() - 1), -1});
    uint32_t lcg = 12345;
    for (int k = 0; k < 600; k++) {
        lcg = lcg * 1103515245u + 12345u;
        const size_t off = (lcg >> 8) % v.size();
        lcg = lcg * 1103515245u + 12345u;
        Bytes b = v;
        b[off] = uint8_t(b[off] ^ (1u + ((lcg >> 16) % 255u)));
        out.push_back({"flip byte " + std::to_string(off), b, -1});
    }
    return out;
}

} // namespace vpx_fixtures
