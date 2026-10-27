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

// Table signature for authored Visual Pinball X tables, version vts1: a digest over the rules a
// table plays by, so an unmodified copy matches its registration and a customised one does not.
// Header-only, C++17, no dependencies. Callers read the .vpx container themselves and pass in the
// script, game items and physics records; this file does no file I/O. See README.md.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace table_signature {

constexpr const char *kVersion = "vts1";

// ---------------------------------------------------------------- SHA-256

using Digest = std::array<uint8_t, 32>;

class Sha256
{
public:
    Sha256() { reset(); }

    void reset()
    {
        m_h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
               0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        m_len = 0;
        m_used = 0;
    }

    void update(const void *data, size_t size)
    {
        const auto *p = static_cast<const uint8_t *>(data);
        m_len += size;
        while (size > 0) {
            const size_t n = std::min(size, sizeof m_block - m_used);
            std::memcpy(m_block + m_used, p, n);
            m_used += n;
            p += n;
            size -= n;
            if (m_used == sizeof m_block) {
                compress();
                m_used = 0;
            }
        }
    }

    void update(std::string_view s) { update(s.data(), s.size()); }

    auto finish() -> Digest
    {
        const uint64_t bits = m_len * 8;
        const uint8_t pad = 0x80;
        update(&pad, 1);
        const uint8_t zero = 0;
        while (m_used != 56) {
            update(&zero, 1);
        }
        uint8_t len[8];
        for (int i = 0; i < 8; i++) {
            len[i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
        }
        update(len, 8);
        Digest out {};
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 4; j++) {
                out[4 * i + j] = static_cast<uint8_t>(m_h[i] >> (24 - 8 * j));
            }
        }
        return out;
    }

private:
    static auto rotr(uint32_t x, int n) -> uint32_t { return (x >> n) | (x << (32 - n)); }

    void compress()
    {
        static constexpr uint32_t k[64] = {
                0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
                0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
                0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
                0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
                0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
                0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
                0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
                0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
                0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
                0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t(m_block[4 * i]) << 24) | (uint32_t(m_block[4 * i + 1]) << 16)
                   | (uint32_t(m_block[4 * i + 2]) << 8) | uint32_t(m_block[4 * i + 3]);
        }
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        auto v = m_h;
        for (int i = 0; i < 64; i++) {
            const uint32_t s1 = rotr(v[4], 6) ^ rotr(v[4], 11) ^ rotr(v[4], 25);
            const uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr(v[0], 2) ^ rotr(v[0], 13) ^ rotr(v[0], 22);
            const uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const uint32_t t2 = s0 + maj;
            v = {t1 + t2, v[0], v[1], v[2], v[3] + t1, v[4], v[5], v[6]};
        }
        for (int i = 0; i < 8; i++) {
            m_h[i] += v[i];
        }
    }

    std::array<uint32_t, 8> m_h {};
    uint8_t m_block[64] {};
    size_t m_used = 0;
    uint64_t m_len = 0;
};

inline auto hex(const uint8_t *data, size_t size) -> std::string
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; i++) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0xf];
    }
    return out;
}

inline auto hex(const Digest &d) -> std::string { return hex(d.data(), d.size()); }

// ---------------------------------------------------------------- inputs

/// One BIFF field as stored: its tag (e.g. "TMIN") and value bytes exactly as written.
struct Field
{
    std::string tag;
    std::vector<uint8_t> value;
};

/// One GameItemN stream: its ItemTypeEnum, its NAME (UTF-8) and all its fields.
struct Item
{
    uint32_t type = 0;
    std::string name;
    std::vector<Field> fields;
};

/// One physics material from the PHMA record.
struct PhysicsMaterial
{
    std::string name;
    float elasticity = 0, elasticityFalloff = 0, friction = 0, scatterAngle = 0;
};

/// A script the table loads from its own folder by a literal GetTextFile/LoadScript name.
struct HelperScript
{
    std::string name;
    std::string content;
};

struct Inputs
{
    /// The script that runs: the sidecar .vbs if present, else the CODE record, as raw bytes.
    std::string script;
    /// Scripts named by scriptReferences() that resolve inside the table's own folder.
    std::vector<HelperScript> helperScripts;
    /// Names from scriptReferences() that resolve to scripts shipped with VPX; recorded by name only.
    std::vector<std::string> vpxScripts;
    /// GetTextFile/LoadScript calls whose argument is not a string literal.
    uint32_t dynamicLoads = 0;
    std::vector<Item> items;
    /// All fields of GameStg/GameData; only the physics allow-list is used.
    std::vector<Field> tableFields;
    std::vector<PhysicsMaterial> materials;
};

// ---------------------------------------------------------------- allow-lists (vts1)

// ItemTypeEnum values, vpinball src/core/iselect.h.
enum : uint32_t {
    kSurface = 0, kFlipper = 1, kPlunger = 3, kBumper = 5, kTrigger = 6, kLight = 7, kKicker = 8,
    kGate = 10, kSpinner = 11, kRamp = 12, kPrimitive = 19, kRubber = 21, kHitTarget = 22, kBall = 23,
};

/// Every named item contributes its timer; these types add their rule-bearing fields.
inline auto itemAllowList(uint32_t type) -> std::vector<std::string_view>
{
    switch (type) {
    case kSurface:
        return {"HTEV", "DROP", "CLDW", "ISBS", "THRS", "SLGF", "SLTH", "ELAS", "ELFO", "WFCT", "WSCT", "OVPH"};
    case kFlipper:
        return {"BASR", "ENDR", "FLPR", "FRMN", "FAEO", "FRTN", "ANGS", "ANGE", "OVRP", "FORC",
                "STRG", "ELAS", "ELFO", "FRIC", "RPUP", "SCTR", "TODA", "TDAA", "ENBL", "FHGT"};
    case kPlunger:
        return {"HPSL", "SPDP", "SPDF", "MEST", "MECH", "APLG", "MPRK", "PSCV", "MOMX"};
    case kBumper:
        return {"RADI", "THRS", "FORC", "BSCT", "HAHE", "COLI"};
    case kTrigger:
        return {"RADI", "EBLD", "THOT", "SHAP"};
    case kLight:
        return {"STAT", "STTF", "BPAT", "BINT"};
    case kKicker:
        return {"RADI", "EBLD", "TYPE", "KSCT", "KHAC", "KHHI", "FATH", "LEMO"};
    case kGate:
        return {"GCOL", "ELAS", "GAMA", "GAMI", "GFRC", "AFRC", "GGFC", "TWWA"};
    case kSpinner:
        return {"AFRC", "SMAX", "SMIN", "SELA"};
    case kRamp:
        return {"HTEV", "THRS", "ELAS", "RFCT", "RSCT", "CLDR", "OVPH"};
    case kPrimitive:
        return {"HTEV", "THRS", "ELAS", "ELFO", "RFCT", "RSCT", "CLDR", "ISTO", "OVPH"};
    case kRubber:
        return {"HTHI", "HTEV", "ELAS", "ELFO", "RFCT", "RSCT", "CLDR", "OVPH"};
    case kHitTarget:
        return {"TRTY", "LEMO", "HTEV", "THRS", "ELAS", "ELFO", "RFCT", "RSCT", "CLDR", "ISDR",
                "DRSP", "RADE", "OVPH"};
    case kBall:
        return {"RADI", "MASS"};
    default:
        return {};
    }
}

inline auto timerAllowList() -> std::vector<std::string_view> { return {"TMON", "TMIN"}; }

/// GameStg/GameData physics.
inline auto tableAllowList() -> std::vector<std::string_view>
{
    return {"ORRP", "ORPF", "GAVT", "FRCT", "ELAS", "ELFA", "PFSC", "SCAT", "NDGT", "PHML",
            "SLOP", "SLPX", "TDFT"};
}

// ---------------------------------------------------------------- script canonical form

/// UTF-8 if valid, else ISO-8859-1 converted to UTF-8, as VPX loads a script.
inline auto decodeScript(std::string_view raw) -> std::string
{
    auto validUtf8 = [](std::string_view s) {
        size_t i = 0;
        while (i < s.size()) {
            const auto c = static_cast<uint8_t>(s[i]);
            size_t follow = 0;
            if (c >= 0x80) {
                if ((c >> 5) == 0x6) {
                    follow = 1;
                } else if ((c >> 4) == 0xe) {
                    follow = 2;
                } else if ((c >> 3) == 0x1e) {
                    follow = 3;
                } else {
                    return false;
                }
            }
            for (size_t k = 1; k <= follow; k++) {
                if (i + k >= s.size() || (static_cast<uint8_t>(s[i + k]) >> 6) != 0x2) {
                    return false;
                }
            }
            i += follow + 1;
        }
        return true;
    };
    if (validUtf8(raw)) {
        return std::string(raw);
    }
    std::string out;
    for (const char ch : raw) {
        const auto c = static_cast<uint8_t>(ch);
        if (c < 0x80) {
            out += ch;
        } else {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

constexpr char kTokenSep = '\x1f';
constexpr char kStatementSep = '\x1e';

/// The script's tokens: comments and whitespace gone, continuations joined, statements separated by
/// one kStatementSep, everything but string and date literals lower-cased.
inline auto tokenize(std::string_view utf8) -> std::vector<std::string>
{
    std::vector<std::string> out;
    auto separator = [&] {
        if (!out.empty() && out.back() != std::string(1, kStatementSep)) {
            out.emplace_back(1, kStatementSep);
        }
    };
    auto lower = [](std::string s) {
        for (auto &c : s) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return s;
    };
    auto isWord = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'
               || static_cast<uint8_t>(c) >= 0x80;
    };
    auto isEol = [](char c) { return c == '\n' || c == '\r'; };
    const size_t n = utf8.size();
    size_t i = 0;
    auto toEol = [&] {
        while (i < n && !isEol(utf8[i])) {
            i++;
        }
    };
    while (i < n) {
        const char c = utf8[i];
        if (isEol(c)) {
            separator();
            i += (c == '\r' && i + 1 < n && utf8[i + 1] == '\n') ? 2 : 1;
        } else if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
            i++;
        } else if (c == '\'') {
            toEol();
        } else if (c == ':') {
            separator();
            i++;
        } else if (c == '_' && (i + 1 == n || !isWord(utf8[i + 1]))) {
            // " _" then only blanks to the end of the line: a continuation
            size_t j = i + 1;
            while (j < n && (utf8[j] == ' ' || utf8[j] == '\t')) {
                j++;
            }
            if (j == n || isEol(utf8[j])) {
                i = j;
                if (i < n) {
                    i += (utf8[i] == '\r' && i + 1 < n && utf8[i + 1] == '\n') ? 2 : 1;
                }
            } else {
                out.emplace_back("_");
                i++;
            }
        } else if (c == '"') {
            size_t j = i + 1;
            while (j < n && !isEol(utf8[j])) {
                if (utf8[j] == '"') {
                    if (j + 1 < n && utf8[j + 1] == '"') {
                        j += 2;
                        continue;
                    }
                    j++;
                    break;
                }
                j++;
            }
            out.emplace_back(utf8.substr(i, j - i));
            i = j;
        } else if (c == '#') {
            size_t j = i + 1;
            while (j < n && !isEol(utf8[j]) && utf8[j] != '#') {
                j++;
            }
            if (j < n && utf8[j] == '#') {
                j++;
            }
            out.emplace_back(utf8.substr(i, j - i));
            i = j;
        } else if (c == '[') {
            size_t j = i + 1;
            while (j < n && !isEol(utf8[j]) && utf8[j] != ']') {
                j++;
            }
            if (j < n && utf8[j] == ']') {
                j++;
            }
            out.push_back(lower(std::string(utf8.substr(i, j - i))));
            i = j;
        } else if (c == '&' && i + 1 < n && (utf8[i + 1] == 'h' || utf8[i + 1] == 'H' || utf8[i + 1] == 'o' || utf8[i + 1] == 'O')) {
            size_t j = i + 2;
            while (j < n && isWord(utf8[j])) {
                j++;
            }
            if (j < n && utf8[j] == '&') {
                j++;
            }
            out.push_back(lower(std::string(utf8.substr(i, j - i))));
            i = j;
        } else if ((c >= '0' && c <= '9') || (c == '.' && i + 1 < n && utf8[i + 1] >= '0' && utf8[i + 1] <= '9')) {
            size_t j = i;
            while (j < n && ((utf8[j] >= '0' && utf8[j] <= '9') || utf8[j] == '.')) {
                j++;
            }
            if (j < n && (utf8[j] == 'e' || utf8[j] == 'E')) {
                size_t k = j + 1;
                if (k < n && (utf8[k] == '+' || utf8[k] == '-')) {
                    k++;
                }
                if (k < n && utf8[k] >= '0' && utf8[k] <= '9') {
                    j = k;
                    while (j < n && utf8[j] >= '0' && utf8[j] <= '9') {
                        j++;
                    }
                }
            }
            out.push_back(lower(std::string(utf8.substr(i, j - i))));
            i = j;
        } else if (isWord(c)) {
            size_t j = i;
            while (j < n && isWord(utf8[j])) {
                j++;
            }
            auto word = lower(std::string(utf8.substr(i, j - i)));
            if (word == "rem") {
                toEol();
            } else {
                out.push_back(std::move(word));
                i = j;
            }
        } else {
            const std::string_view two = utf8.substr(i, 2);
            if (two == "<=" || two == ">=" || two == "<>") {
                out.emplace_back(two);
                i += 2;
            } else {
                out.emplace_back(1, c);
                i++;
            }
        }
    }
    while (!out.empty() && out.back() == std::string(1, kStatementSep)) {
        out.pop_back();
    }
    return out;
}

inline auto canonicalScript(const std::vector<std::string> &tokens) -> std::string
{
    std::string out;
    for (size_t i = 0; i < tokens.size(); i++) {
        if (i > 0 && tokens[i] != std::string(1, kStatementSep) && tokens[i - 1] != std::string(1, kStatementSep)) {
            out += kTokenSep;
        }
        out += tokens[i];
    }
    return out;
}

/// Script files a script loads: literal GetTextFile("x") / LoadScript "x" arguments, lower-cased, in
/// order of first use, and a count of calls whose argument is not a literal.
struct ScriptReferences
{
    std::vector<std::string> names;
    uint32_t dynamic = 0;
};

inline auto scriptReferences(const std::vector<std::string> &tokens) -> ScriptReferences
{
    ScriptReferences out;
    for (size_t i = 0; i < tokens.size(); i++) {
        if (tokens[i] != "gettextfile" && tokens[i] != "loadscript") {
            continue;
        }
        size_t j = i + 1;
        if (j < tokens.size() && tokens[j] == "(") {
            j++;
        }
        const bool literal = j < tokens.size() && tokens[j].size() >= 2 && tokens[j].front() == '"'
                             && (j + 1 == tokens.size() || tokens[j + 1] == ")"
                                 || tokens[j + 1] == std::string(1, kStatementSep));
        if (!literal) {
            out.dynamic++;
            continue;
        }
        std::string name;
        const auto &t = tokens[j];
        for (size_t k = 1; k + 1 < t.size(); k++) {
            name += (t[k] >= 'A' && t[k] <= 'Z') ? static_cast<char>(t[k] - 'A' + 'a') : t[k];
            if (t[k] == '"') {
                k++; // "" is one quote
            }
        }
        if (std::find(out.names.begin(), out.names.end(), name) == out.names.end()) {
            out.names.push_back(name);
        }
    }
    return out;
}

/// A procedure's key and the first 8 bytes of the digest over its canonical tokens.
struct Procedure
{
    std::string key;
    std::array<uint8_t, 8> digest {};
};

/// Sub, Function and Property blocks keyed by lower-cased name ("class.name" inside a Class), with
/// code outside any procedure as key "". Repeated keys get "#2", "#3" in order. Sorted by key.
inline auto procedures(const std::vector<std::string> &tokens) -> std::vector<Procedure>
{
    std::vector<std::vector<std::string>> statements(1);
    for (const auto &t : tokens) {
        if (t == std::string(1, kStatementSep)) {
            statements.emplace_back();
        } else {
            statements.back().push_back(t);
        }
    }
    std::map<std::string, std::vector<std::string>> blocks;
    std::vector<std::string> order;
    auto append = [&](const std::string &key, const std::vector<std::string> &st) {
        auto &b = blocks[key];
        if (!b.empty()) {
            b.emplace_back(1, kStatementSep);
        }
        b.insert(b.end(), st.begin(), st.end());
    };
    std::string cls;
    std::string current;
    bool inProc = false;
    std::map<std::string, int> seen;
    for (const auto &st : statements) {
        if (st.empty()) {
            continue;
        }
        size_t k = 0;
        while (k < st.size() && (st[k] == "public" || st[k] == "private" || st[k] == "default")) {
            k++;
        }
        const bool isClass = k < st.size() && st[k] == "class" && k + 1 < st.size();
        const bool isEndClass = st.size() >= 2 && st[0] == "end" && st[1] == "class";
        const bool opens = !inProc && k < st.size() && (st[k] == "sub" || st[k] == "function" || st[k] == "property");
        if (!inProc && isClass) {
            cls = st[k + 1];
            append("", st);
            continue;
        }
        if (!inProc && isEndClass) {
            cls.clear();
            append("", st);
            continue;
        }
        if (opens) {
            size_t nameAt = k + 1;
            if (st[k] == "property" && nameAt < st.size() && (st[nameAt] == "get" || st[nameAt] == "let" || st[nameAt] == "set")) {
                nameAt++;
            }
            std::string name = nameAt < st.size() ? st[nameAt] : std::string();
            if (st[k] == "property" && k + 1 < st.size() && st[k + 1] != name) {
                name += " " + st[k + 1];
            }
            std::string key = cls.empty() ? name : cls + "." + name;
            const int count = ++seen[key];
            if (count > 1) {
                key += "#" + std::to_string(count);
            }
            current = key;
            inProc = true;
            append(current, st);
            continue;
        }
        if (inProc) {
            append(current, st);
            if (st.size() >= 2 && st[0] == "end" && (st[1] == "sub" || st[1] == "function" || st[1] == "property")) {
                inProc = false;
            }
            continue;
        }
        append("", st);
    }
    std::vector<Procedure> out;
    for (const auto &[key, toks] : blocks) {
        Sha256 h;
        h.update(canonicalScript(toks));
        const auto d = h.finish();
        Procedure p;
        p.key = key;
        std::copy(d.begin(), d.begin() + 8, p.digest.begin());
        out.push_back(std::move(p));
    }
    return out;
}

// ---------------------------------------------------------------- digests

struct Signature
{
    std::string version = kVersion;
    Digest top {}, script {}, items {}, physics {};
    std::vector<Procedure> procedures;
};

namespace detail {

inline void putU32(Sha256 &h, uint32_t v)
{
    const uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
    h.update(b, 4);
}

inline void putBytes(Sha256 &h, std::string_view s)
{
    putU32(h, static_cast<uint32_t>(s.size()));
    h.update(s);
}

inline auto lower(std::string s) -> std::string
{
    for (auto &c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

/// Each allow-listed tag in order, present or explicitly absent.
inline void putFields(Sha256 &h, const std::vector<Field> &fields, const std::vector<std::string_view> &allow)
{
    for (const auto tag : allow) {
        h.update(tag);
        const auto it = std::find_if(fields.begin(), fields.end(), [&](const Field &f) { return f.tag == tag; });
        if (it == fields.end()) {
            const uint8_t absent = 0;
            h.update(&absent, 1);
        } else {
            const uint8_t present = 1;
            h.update(&present, 1);
            putBytes(h, std::string_view(reinterpret_cast<const char *>(it->value.data()), it->value.size()));
        }
    }
}

} // namespace detail

/// The main script's canonical form, then each helper (sorted by name) as name and canonical form,
/// then the VPX-shipped names (sorted), then the dynamic-load count.
inline auto scriptDigest(const std::vector<std::string> &tokens, std::vector<HelperScript> helpers,
                         std::vector<std::string> vpxScripts, uint32_t dynamicLoads) -> Digest
{
    Sha256 h;
    detail::putBytes(h, canonicalScript(tokens));
    for (auto &helper : helpers) {
        helper.name = detail::lower(helper.name);
    }
    std::sort(helpers.begin(), helpers.end(),
              [](const HelperScript &a, const HelperScript &b) { return a.name < b.name; });
    detail::putU32(h, static_cast<uint32_t>(helpers.size()));
    for (const auto &helper : helpers) {
        detail::putBytes(h, helper.name);
        detail::putBytes(h, canonicalScript(tokenize(decodeScript(helper.content))));
    }
    for (auto &name : vpxScripts) {
        name = detail::lower(name);
    }
    std::sort(vpxScripts.begin(), vpxScripts.end());
    detail::putU32(h, static_cast<uint32_t>(vpxScripts.size()));
    for (const auto &name : vpxScripts) {
        detail::putBytes(h, name);
    }
    detail::putU32(h, dynamicLoads);
    return h.finish();
}

inline auto itemsDigest(std::vector<Item> items) -> Digest
{
    for (auto &item : items) {
        item.name = detail::lower(item.name);
    }
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        return a.type != b.type ? a.type < b.type : a.name < b.name;
    });
    Sha256 h;
    for (const auto &item : items) {
        detail::putU32(h, item.type);
        detail::putBytes(h, item.name);
        detail::putFields(h, item.fields, timerAllowList());
        detail::putFields(h, item.fields, itemAllowList(item.type));
    }
    return h.finish();
}

inline auto physicsDigest(const std::vector<Field> &tableFields, std::vector<PhysicsMaterial> materials) -> Digest
{
    Sha256 h;
    detail::putFields(h, tableFields, tableAllowList());
    for (auto &m : materials) {
        m.name = detail::lower(m.name);
    }
    std::sort(materials.begin(), materials.end(),
              [](const PhysicsMaterial &a, const PhysicsMaterial &b) { return a.name < b.name; });
    for (const auto &m : materials) {
        detail::putBytes(h, m.name);
        for (const float f : {m.elasticity, m.elasticityFalloff, m.friction, m.scatterAngle}) {
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            detail::putU32(h, bits);
        }
    }
    return h.finish();
}

inline auto compute(const Inputs &in) -> Signature
{
    Signature s;
    const auto tokens = tokenize(decodeScript(in.script));
    s.script = scriptDigest(tokens, in.helperScripts, in.vpxScripts, in.dynamicLoads);
    s.items = itemsDigest(in.items);
    s.physics = physicsDigest(in.tableFields, in.materials);
    s.procedures = procedures(tokens);
    for (const auto &helper : in.helperScripts) {
        for (auto p : procedures(tokenize(decodeScript(helper.content)))) {
            p.key = detail::lower(helper.name) + ":" + p.key;
            s.procedures.push_back(std::move(p));
        }
    }
    std::sort(s.procedures.begin(), s.procedures.end(),
              [](const Procedure &a, const Procedure &b) { return a.key < b.key; });
    Sha256 h;
    h.update(kVersion, std::strlen(kVersion) + 1);
    h.update(s.script.data(), s.script.size());
    h.update(s.items.data(), s.items.size());
    h.update(s.physics.data(), s.physics.size());
    s.top = h.finish();
    return s;
}

} // namespace table_signature
