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

// A small zlib (RFC 1950) / DEFLATE (RFC 1951) decoder for untrusted input, written for this
// library in the canonical-Huffman style of Mark Adler's puff.c. Header-only, C++17, no
// dependencies. Every read is bounds-checked, the output never exceeds the caller's cap, and
// nothing recurses or throws except std::bad_alloc from the output vector.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace table_signature::inflate {

enum class Status { ok, malformed, too_large };

namespace detail {

constexpr int kMaxBits = 15;
constexpr int kMaxLitLen = 286;
constexpr int kMaxDist = 30;
constexpr int kFixedLitLen = 288;

constexpr uint16_t kLengthBase[29] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                      15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                      67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                      2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct Huffman {
    uint16_t count[kMaxBits + 1] {};
    uint16_t symbol[kFixedLitLen] {};
};

class Decoder
{
public:
    Decoder(const uint8_t *in, size_t size, size_t maxOut, std::vector<uint8_t> &out)
        : m_in(in)
        , m_size(size)
        , m_maxOut(maxOut)
        , m_out(out)
    {
    }

    auto run() -> Status
    {
        int last = 0;
        do {
            last = bits(1);
            const int type = bits(2);
            Status s = Status::malformed;
            if (m_overrun) {
                return Status::malformed;
            }
            if (type == 0) {
                s = stored();
            } else if (type == 1) {
                s = fixed();
            } else if (type == 2) {
                s = dynamic();
            }
            if (s != Status::ok) {
                return s;
            }
        } while (!last);
        return Status::ok;
    }

    /// Byte position after the last whole byte consumed (the trailer starts here).
    auto position() const -> size_t { return m_pos; }

private:
    auto bits(int need) -> int
    {
        uint32_t val = m_bitBuf;
        while (m_bitCount < need) {
            if (m_pos >= m_size) {
                m_overrun = true;
                return 0;
            }
            val |= static_cast<uint32_t>(m_in[m_pos++]) << m_bitCount;
            m_bitCount += 8;
        }
        m_bitBuf = val >> need;
        m_bitCount -= need;
        return static_cast<int>(val & ((1u << need) - 1));
    }

    auto put(uint8_t b) -> bool
    {
        if (m_out.size() >= m_maxOut) {
            return false;
        }
        m_out.push_back(b);
        return true;
    }

    auto stored() -> Status
    {
        m_bitBuf = 0;
        m_bitCount = 0;
        if (m_size - m_pos < 4) {
            return Status::malformed;
        }
        const unsigned len = m_in[m_pos] | (m_in[m_pos + 1] << 8);
        const unsigned nlen = m_in[m_pos + 2] | (m_in[m_pos + 3] << 8);
        m_pos += 4;
        if (len != (~nlen & 0xffffu) || m_size - m_pos < len) {
            return Status::malformed;
        }
        for (unsigned i = 0; i < len; i++) {
            if (!put(m_in[m_pos++])) {
                return Status::too_large;
            }
        }
        return Status::ok;
    }

    // Canonical decode: at most kMaxBits iterations; -1 for an unused code or truncated input.
    auto decode(const Huffman &h) -> int
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; len++) {
            code |= bits(1);
            if (m_overrun) {
                return -1;
            }
            const int count = h.count[len];
            if (code - count < first) {
                return h.symbol[index + (code - first)];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    // Returns <0 if over-subscribed, 0 if complete, >0 if incomplete.
    static auto construct(Huffman &h, const uint16_t *length, int n) -> int
    {
        for (auto &c : h.count) {
            c = 0;
        }
        for (int s = 0; s < n; s++) {
            h.count[length[s]]++;
        }
        if (h.count[0] == n) {
            return 0;
        }
        int left = 1;
        for (int len = 1; len <= kMaxBits; len++) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) {
                return left;
            }
        }
        uint16_t offs[kMaxBits + 1] {};
        for (int len = 1; len < kMaxBits; len++) {
            offs[len + 1] = static_cast<uint16_t>(offs[len] + h.count[len]);
        }
        for (int s = 0; s < n; s++) {
            if (length[s] != 0) {
                h.symbol[offs[length[s]]++] = static_cast<uint16_t>(s);
            }
        }
        return left;
    }

    auto codes(const Huffman &lencode, const Huffman &distcode) -> Status
    {
        for (;;) {
            int sym = decode(lencode);
            if (sym < 0) {
                return Status::malformed;
            }
            if (sym < 256) {
                if (!put(static_cast<uint8_t>(sym))) {
                    return Status::too_large;
                }
                continue;
            }
            if (sym == 256) {
                return Status::ok;
            }
            sym -= 257;
            if (sym >= 29) {
                return Status::malformed;
            }
            const size_t len = kLengthBase[sym] + static_cast<size_t>(bits(kLengthExtra[sym]));
            const int dsym = decode(distcode);
            if (dsym < 0 || dsym >= kMaxDist) {
                return Status::malformed;
            }
            const size_t dist = kDistBase[dsym] + static_cast<size_t>(bits(kDistExtra[dsym]));
            if (m_overrun || dist > m_out.size()) {
                return Status::malformed;
            }
            for (size_t i = 0; i < len; i++) {
                if (!put(m_out[m_out.size() - dist])) {
                    return Status::too_large;
                }
            }
        }
    }

    auto fixed() -> Status
    {
        Huffman lencode, distcode;
        uint16_t lengths[kFixedLitLen];
        int s = 0;
        for (; s < 144; s++) {
            lengths[s] = 8;
        }
        for (; s < 256; s++) {
            lengths[s] = 9;
        }
        for (; s < 280; s++) {
            lengths[s] = 7;
        }
        for (; s < kFixedLitLen; s++) {
            lengths[s] = 8;
        }
        construct(lencode, lengths, kFixedLitLen);
        for (s = 0; s < kMaxDist; s++) {
            lengths[s] = 5;
        }
        construct(distcode, lengths, kMaxDist);
        return codes(lencode, distcode);
    }

    auto dynamic() -> Status
    {
        static constexpr uint8_t order[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                              11, 4,  12, 3, 13, 2, 14, 1, 15};
        uint16_t lengths[kMaxLitLen + kMaxDist] {};
        const int nlen = bits(5) + 257;
        const int ndist = bits(5) + 1;
        const int ncode = bits(4) + 4;
        if (m_overrun || nlen > kMaxLitLen || ndist > kMaxDist) {
            return Status::malformed;
        }
        int index = 0;
        for (; index < ncode; index++) {
            lengths[order[index]] = static_cast<uint16_t>(bits(3));
        }
        for (; index < 19; index++) {
            lengths[order[index]] = 0;
        }
        Huffman lencode, distcode;
        if (m_overrun || construct(lencode, lengths, 19) != 0) {
            return Status::malformed;
        }
        index = 0;
        while (index < nlen + ndist) {
            int sym = decode(lencode);
            if (sym < 0) {
                return Status::malformed;
            }
            if (sym < 16) {
                lengths[index++] = static_cast<uint16_t>(sym);
                continue;
            }
            uint16_t len = 0;
            if (sym == 16) {
                if (index == 0) {
                    return Status::malformed;
                }
                len = lengths[index - 1];
                sym = 3 + bits(2);
            } else if (sym == 17) {
                sym = 3 + bits(3);
            } else {
                sym = 11 + bits(7);
            }
            if (m_overrun || index + sym > nlen + ndist) {
                return Status::malformed;
            }
            while (sym--) {
                lengths[index++] = len;
            }
        }
        if (lengths[256] == 0) {
            return Status::malformed;
        }
        // An incomplete code is allowed only when it has a single symbol (RFC 1951, puff.c).
        int err = construct(lencode, lengths, nlen);
        if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1)) {
            return Status::malformed;
        }
        err = construct(distcode, lengths + nlen, ndist);
        if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1)) {
            return Status::malformed;
        }
        return codes(lencode, distcode);
    }

    const uint8_t *m_in;
    size_t m_size;
    size_t m_pos = 0;
    uint32_t m_bitBuf = 0;
    int m_bitCount = 0;
    bool m_overrun = false;
    size_t m_maxOut;
    std::vector<uint8_t> &m_out;
};

inline auto adler32(const uint8_t *data, size_t size) -> uint32_t
{
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < size; i++) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

} // namespace detail

/// Inflates a zlib stream into `out` (cleared first), writing at most `maxOut` bytes. too_large
/// means the data would inflate past the cap; malformed covers bad headers, codes and checksums.
inline auto zlib(const uint8_t *in, size_t size, size_t maxOut, std::vector<uint8_t> &out) -> Status
{
    out.clear();
    if (in == nullptr || size < 6) {
        return Status::malformed;
    }
    const unsigned cmf = in[0], flg = in[1];
    if ((cmf & 0x0f) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20) != 0) {
        return Status::malformed;
    }
    detail::Decoder d(in + 2, size - 2, maxOut, out);
    const Status s = d.run();
    if (s != Status::ok) {
        return s;
    }
    const size_t trailer = 2 + d.position();
    if (size - trailer < 4) {
        return Status::malformed;
    }
    const uint32_t want = (uint32_t(in[trailer]) << 24) | (uint32_t(in[trailer + 1]) << 16)
                        | (uint32_t(in[trailer + 2]) << 8) | uint32_t(in[trailer + 3]);
    return detail::adler32(out.data(), out.size()) == want ? Status::ok : Status::malformed;
}

} // namespace table_signature::inflate
