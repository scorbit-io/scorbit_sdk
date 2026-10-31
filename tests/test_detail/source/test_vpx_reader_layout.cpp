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

#include <table_signature/vpx_reader.h>
#include <vpx_writer.h>

#include <catch2/catch_test_macros.hpp>

using namespace table_signature;
using vpx::Code;

namespace {

// An unused stream of blobBytes pads the file: the reader bounds the FAT size by the file size, so
// a DIFAT (more than 109 FAT sectors) needs a file of about 7 MB at 512-byte sectors.
auto build(vpx_fixtures::CfbOptions opts, size_t blobBytes = 0) -> vpx_fixtures::Built
{
    auto streams = vpx_fixtures::streams(vpx_fixtures::table());
    if (blobBytes) {
        streams.push_back({"Blob", vpx_fixtures::Bytes(blobBytes, 0x5A)});
    }
    return vpx_fixtures::writeCfb(streams, opts);
}

auto topOf(const vpx_fixtures::Bytes &b) -> std::string
{
    const auto r = vpx::parse(b.data(), b.size());
    REQUIRE(r.code == Code::ok);
    return hex(compute(r.inputs).top);
}

auto rd32(const vpx_fixtures::Bytes &b, size_t p) -> uint32_t
{
    return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
           uint32_t(b[p + 3]) << 24;
}

auto wr32(vpx_fixtures::Bytes &b, size_t p, uint32_t v) -> void
{
    for (int i = 0; i < 4; i++) {
        b[p + i] = uint8_t(v >> (8 * i));
    }
}

auto code(const vpx_fixtures::Bytes &b) -> Code
{
    return vpx::parse(b.data(), b.size()).code;
}

} // namespace

constexpr size_t kDifatBlob = size_t(8) << 20;     // 129 FAT sectors: one DIFAT sector
constexpr size_t kTwoDifatBlob = size_t(17) << 20; // 260 FAT sectors: two DIFAT sectors

TEST_CASE("vpx reader follows a DIFAT chain", "[vpx_reader]")
{
    const auto expected = topOf(build({}).bytes);
    const auto b = build({false, 120}, kDifatBlob);
    CHECK(rd32(b.bytes, 0x48) == 1);
    CHECK(topOf(b.bytes) == expected);
    const auto two = build({}, kTwoDifatBlob);
    CHECK(rd32(two.bytes, 0x48) == 2);
    CHECK(topOf(two.bytes) == expected);
}

TEST_CASE("vpx reader reads version 4 files", "[vpx_reader]")
{
    const auto expected = topOf(build({}).bytes);
    const auto plain = build({true, 0});
    CHECK(plain.bytes[0x1A] == 4);
    CHECK(plain.bytes.size() % 4096 == 0);
    CHECK(topOf(plain.bytes) == expected);

    const auto big = build({true, 0}, kDifatBlob);
    CHECK(topOf(big.bytes) == expected);
}

TEST_CASE("vpx reader rejects a corrupt DIFAT chain", "[vpx_reader]")
{
    const auto b = build({}, kTwoDifatBlob);
    REQUIRE(rd32(b.bytes, 0x48) == 2);
    const uint32_t first = rd32(b.bytes, 0x44);
    const size_t next = vpx_fixtures::sectorOffset(first) + 508;
    REQUIRE(rd32(b.bytes, next) == first + 1);

    auto loop = b.bytes;
    wr32(loop, next, first);
    CHECK(code(loop) == Code::malformed);

    auto past = b.bytes;
    wr32(past, next, 0x00FFFFF0);
    CHECK(code(past) == Code::malformed);

    auto count = b.bytes;
    wr32(count, 0x48, 1); // the chain needs both sectors
    CHECK(code(count) == Code::malformed);

    auto short_ = b.bytes;
    wr32(short_, next, 0xFFFFFFFE); // ends early
    CHECK(code(short_) == Code::malformed);
}

TEST_CASE("vpx reader rejects a version 4 file cut mid-sector", "[vpx_reader]")
{
    const auto whole = build({true, 0}).bytes;
    for (const size_t keep : {size_t(4096 + 1000), size_t(3 * 4096 + 1000)}) {
        auto bytes = whole;
        bytes.resize(keep);
        INFO(keep);
        CHECK(code(bytes) != Code::ok);
    }
}
