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

#include <table_signature/inflate.h>
#include <table_signature/vpx_reader.h>
#include <vpx_writer.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#ifndef _WIN32
#    include <sys/stat.h>
#endif

using namespace table_signature;
using vpx::Code;
namespace fs = std::filesystem;

namespace {

const fs::path kRoot = TABLE_SIGNATURE_VPX_FIXTURES;
const fs::path kFixtures = kRoot / "fixtures" / "vpx";

/// A scratch folder name unique to this run, so test runs from two build trees do not collide.
auto scratch(const std::string &name) -> fs::path
{
    static const std::string run = std::to_string(std::random_device {}());
    return fs::temp_directory_path() / ("vpx_reader_" + run + "_" + name);
}

auto readBytes(const fs::path &p) -> std::vector<uint8_t>
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

auto unhex(const std::string &s) -> std::vector<uint8_t>
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
    }
    return out;
}

auto paths(const std::string &fixture) -> vpx::Paths
{
    return {(kFixtures / fixture / "table.vpx").string(), "", (kFixtures / "app").string()};
}

auto parseFile(const fs::path &p, const vpx::Options &opt = {}) -> vpx::Contents
{
    const auto b = readBytes(p);
    return vpx::parse(b.data(), b.size(), opt);
}

void requireSameFields(const std::vector<Field> &a, const std::vector<Field> &b)
{
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); i++) {
        CHECK(a[i].tag == b[i].tag);
        CHECK(a[i].value == b[i].value);
    }
}

} // namespace

TEST_CASE("inflate decodes zlib streams and refuses bad ones", "[vpx_reader]")
{
    std::string text;
    for (int i = 0; i < 40; i++) {
        text += "Sub Rule" + std::to_string(i) + "()\r\n    Score = Score + "
              + std::to_string(i * 7) + "\r\nEnd Sub\r\n";
    }
    const std::vector<uint8_t> plain(text.begin(), text.end());
    // zlib level 9 output for `text`: a dynamic-Huffman block
    const auto dynamic = unhex("78da75d42d6e0341104461bed2de61a1a390e9ea9d3f10980b786fe0c42c4aa4487"
                               "bff041818d41b32e4b14f5dc779dbaee7d7bd"
                               "5c5ed665fb7fc7c7cfef7d7b7bfcaf5b5997f7efcfed386feb723ce2a0b89b5814c"
                               "76eeaa45a61ea1deb61ea4a75565337aa7799"
                               "ba633d4d3da8aecdd493ea960e0729bbb5644ca719c8391c67a0e7749e81a0d3810"
                               "68a4671a481a6110e353af78e35d035e46003"
                               "65231dad0adf92b355706f4f956fb53a5da16e34c7ab9d7be72bf6edce57ec3b9ca"
                               "fd877385fb1ef74be425f15e79b85d7cff966"
                               "70ef7c137d253bc6bcc6e97c93f7389d6fa2af76e79be8abea7cb373ef7c137dd59"
                               "c6fb26f7ff6fd036eabe3d8");
    std::vector<uint8_t> out;
    REQUIRE(inflate::zlib(dynamic.data(), dynamic.size(), 1 << 20, out) == inflate::Status::ok);
    CHECK(out == plain);

    const auto fixed = vpx_fixtures::zlibCompress(plain);
    REQUIRE(inflate::zlib(fixed.data(), fixed.size(), plain.size(), out) == inflate::Status::ok);
    CHECK(out == plain);
    CHECK(inflate::zlib(fixed.data(), fixed.size(), plain.size() - 1, out)
          == inflate::Status::too_large);
    CHECK(out.size() <= plain.size() - 1);

    // A stored block: header, BFINAL + BTYPE 00, LEN/NLEN, data, Adler-32.
    std::vector<uint8_t> stored = {0x78, 0x01, 0x01, 0x03, 0x00, 0xFC, 0xFF, 'a', 'b', 'c'};
    const uint32_t a = inflate::detail::adler32(stored.data() + 7, 3);
    for (int s = 24; s >= 0; s -= 8) {
        stored.push_back(uint8_t(a >> s));
    }
    REQUIRE(inflate::zlib(stored.data(), stored.size(), 16, out) == inflate::Status::ok);
    CHECK(std::string(out.begin(), out.end()) == "abc");

    auto bad = fixed;
    bad.back() ^= 1;
    CHECK(inflate::zlib(bad.data(), bad.size(), 1 << 20, out) == inflate::Status::malformed);
    bad = dynamic;
    bad[1] ^= 1; // header check
    CHECK(inflate::zlib(bad.data(), bad.size(), 1 << 20, out) == inflate::Status::malformed);
    for (size_t n = 0; n < dynamic.size(); n++) {
        CHECK(inflate::zlib(dynamic.data(), n, 1 << 20, out) != inflate::Status::ok);
    }
    CHECK(inflate::zlib(nullptr, 0, 16, out) == inflate::Status::malformed);
}

TEST_CASE("vpx fixtures give their expected signatures", "[vpx_reader]")
{
    std::ifstream f(kRoot / "vpx_fixtures.json");
    REQUIRE(f.good());
    const auto fixtures = nlohmann::json::parse(f);
    REQUIRE(fixtures.size() >= 7);
    for (const auto &fx : fixtures) {
        const std::string name = fx.at("name").get<std::string>();
        INFO(name);
        const auto r = vpx::signFile(paths(name));
        CHECK(vpx::codeName(r.code) == fx.at("code").get<std::string>());
        CHECK(r.missingHelpers == fx.at("missingHelpers").get<std::vector<std::string>>());
        if (r.code == Code::ok) {
            CHECK(hex(r.signature.top) == fx.at("top").get<std::string>());
            CHECK(hex(r.signature.script) == fx.at("script").get<std::string>());
            CHECK(hex(r.signature.items) == fx.at("items").get<std::string>());
            CHECK(hex(r.signature.physics) == fx.at("physics").get<std::string>());
        }
    }
    const auto unmodified = vpx::signFile(paths("unmodified"));
    CHECK(hex(vpx::signFile(paths("cosmetic")).signature.top) == hex(unmodified.signature.top));
    const auto moved = vpx::signFile(paths("moved_bumper"));
    CHECK(hex(moved.signature.items) != hex(unmodified.signature.items));
    CHECK(hex(moved.signature.script) == hex(unmodified.signature.script));
}

TEST_CASE("the reader's inputs for the unmodified fixture match the table it was written from",
          "[vpx_reader]")
{
    const auto model = vpx_fixtures::table();
    // The generator is deterministic and the fixture on disk is its output.
    CHECK(readBytes(kFixtures / "unmodified" / "table.vpx") == vpx_fixtures::build(model).bytes);

    const auto r = vpx::signFile(paths("unmodified"));
    REQUIRE(r.code == Code::ok);
    CHECK_FALSE(r.sidecar);
    CHECK(r.inputs.script == model.inputs.script);
    REQUIRE(r.inputs.helperScripts.size() == 2);
    CHECK(r.inputs.helperScripts[0].name == "helper.vbs");
    CHECK(r.inputs.helperScripts[0].content == vpx_fixtures::kHelper);
    CHECK(r.inputs.helperScripts[1].name == "extra.vbs"); // named by helper.vbs
    CHECK(r.inputs.helperScripts[1].content == vpx_fixtures::kExtra);
    CHECK(r.inputs.vpxScripts == std::vector<std::string> {"core.vbs"});

    REQUIRE(r.inputs.items.size() == model.inputs.items.size());
    for (size_t i = 0; i < r.inputs.items.size(); i++) {
        const auto &got = r.inputs.items[i];
        const auto &want = model.inputs.items[i];
        INFO(want.name);
        CHECK(got.type == want.type);
        CHECK(got.name == want.name);
        requireSameFields(got.fields, want.fields);
        REQUIRE(got.dragPoints.size() == want.dragPoints.size());
        for (size_t p = 0; p < got.dragPoints.size(); p++) {
            requireSameFields(got.dragPoints[p], want.dragPoints[p]);
        }
        CHECK(got.mesh.positions == want.mesh.positions);
        CHECK(got.mesh.indices == want.mesh.indices);
    }
    CHECK(r.inputs.items[0].dragPoints.size() == 4);
    CHECK(r.inputs.items[4].mesh.positions.size() == 72);
    CHECK(r.inputs.items[4].mesh.indices.size() == 36);

    REQUIRE(r.inputs.collections.size() == 1);
    CHECK(r.inputs.collections[0].name == "Targets");
    CHECK(r.inputs.collections[0].members == std::vector<std::string> {"Bumper1", "LeftFlipper"});
    requireSameFields(r.inputs.collections[0].fields, model.inputs.collections[0].fields);

    requireSameFields(r.inputs.tableFields, model.inputs.tableFields);
    REQUIRE(r.inputs.materials.size() == 2);
    for (size_t i = 0; i < 2; i++) {
        CHECK(r.inputs.materials[i].name == model.inputs.materials[i].name);
        CHECK(r.inputs.materials[i].elasticity == model.inputs.materials[i].elasticity);
        CHECK(r.inputs.materials[i].elasticityFalloff
              == model.inputs.materials[i].elasticityFalloff);
        CHECK(r.inputs.materials[i].friction == model.inputs.materials[i].friction);
        CHECK(r.inputs.materials[i].scatterAngle == model.inputs.materials[i].scatterAngle);
    }
}

TEST_CASE("the sidecar script replaces the embedded one and protection", "[vpx_reader]")
{
    const auto r = vpx::signFile(paths("sidecar"));
    REQUIRE(r.code == Code::ok);
    CHECK(r.sidecar);
    CHECK(r.inputs.script == vpx_fixtures::kSidecar);

    const auto prot = parseFile(kFixtures / "protected" / "table.vpx");
    CHECK(prot.code == Code::ok);
    CHECK(prot.scriptProtected);
    CHECK_FALSE(parseFile(kFixtures / "unmodified" / "table.vpx").scriptProtected);
}

namespace {

/// A scratch tree: <root>/<table folder>/table.vpx plus VPX's app and pref folders.
struct Tree {
    fs::path root, table, app, pref;

    explicit Tree(const std::string &name, const std::string &folder = "Table")
        : root(scratch(name))
    {
        fs::remove_all(root);
        table = root / folder;
        app = root / "app";
        pref = root / "pref";
        for (const auto &d : {table, app, pref}) {
            fs::create_directories(d);
        }
        fs::copy_file(kFixtures / "unmodified" / "table.vpx", table / "table.vpx");
    }
    ~Tree() { fs::remove_all(root); }

    void put(const fs::path &p, const std::string &text) const
    {
        fs::create_directories(p.parent_path());
        std::ofstream(p, std::ios::binary) << text;
    }
    auto sign() const -> vpx::Result
    {
        return vpx::signFile({(table / "table.vpx").string(), "", app.string(), pref.string()});
    }
};

auto helperNames(const vpx::Result &r) -> std::vector<std::string>
{
    std::vector<std::string> out;
    for (const auto &h : r.inputs.helperScripts) {
        out.push_back(h.name);
    }
    return out;
}

} // namespace

TEST_CASE("scripts are searched in VPX's folder order", "[vpx_reader]")
{
    const auto reference = vpx::signFile(paths("unmodified"));
    REQUIRE(reference.code == Code::ok);

    SECTION("helpers in the table's user/ and scripts/ are hashed like ones beside it")
    {
        Tree t("table_subfolders");
        t.put(t.table / "User" / "helper.vbs", vpx_fixtures::kHelper);
        t.put(t.table / "scripts" / "EXTRA.vbs", vpx_fixtures::kExtra);
        t.put(t.app / "Scripts" / "core.vbs", "'");
        const auto r = t.sign();
        REQUIRE(r.code == Code::ok);
        CHECK(r.missingHelpers.empty());
        CHECK(hex(r.signature.top) == hex(reference.signature.top));
    }
    SECTION("each VPX folder is searched, name only")
    {
        for (const char *where : {"app", "app/user", "app/scripts", "app/tables", "pref",
                                  "pref/user", "pref/scripts"}) {
            INFO(where);
            Tree t("vpx_folders");
            t.put(t.table / "Helper.vbs", vpx_fixtures::kHelper);
            t.put(t.table / "extra.vbs", vpx_fixtures::kExtra);
            t.put(t.root / where / "core.vbs", "' any content");
            const auto r = t.sign();
            REQUIRE(r.code == Code::ok);
            CHECK(r.inputs.vpxScripts == std::vector<std::string> {"core.vbs"});
            CHECK(hex(r.signature.top) == hex(reference.signature.top));
        }
    }
    SECTION("the table's folder wins over VPX's, and the folder itself over its user/")
    {
        Tree t("precedence");
        t.put(t.table / "Helper.vbs", vpx_fixtures::kHelper);
        t.put(t.table / "user" / "Helper.vbs", "Sub Other() : End Sub\r\n");
        t.put(t.table / "extra.vbs", vpx_fixtures::kExtra);
        t.put(t.app / "extra.vbs", "'");
        t.put(t.pref / "core.vbs", "'");
        const auto r = t.sign();
        REQUIRE(r.code == Code::ok);
        REQUIRE(r.inputs.helperScripts.size() == 2);
        CHECK(r.inputs.helperScripts[0].content == vpx_fixtures::kHelper);
        CHECK(r.inputs.vpxScripts == std::vector<std::string> {"core.vbs"});
        CHECK(hex(r.signature.top) == hex(reference.signature.top));
    }
    SECTION("folders given with a trailing separator")
    {
        const std::string sep(1, char(fs::path::preferred_separator));
        for (const char *where : {"app/scripts", "pref/user"}) {
            INFO(where);
            Tree t("trailing_separator");
            t.put(t.table / "Helper.vbs", vpx_fixtures::kHelper);
            t.put(t.table / "extra.vbs", vpx_fixtures::kExtra);
            t.put(t.root / where / "core.vbs", "'");
            const auto r = vpx::signFile({(t.table / "table.vpx").string(), t.table.string() + sep,
                                          t.app.string() + sep, t.pref.string() + sep});
            REQUIRE(r.code == Code::ok);
            CHECK(r.inputs.vpxScripts == std::vector<std::string> {"core.vbs"});
            CHECK(helperNames(r) == std::vector<std::string> {"helper.vbs", "extra.vbs"});
            CHECK(hex(r.signature.top) == hex(reference.signature.top));
        }
    }
    SECTION("nothing found anywhere is missing")
    {
        Tree t("nothing");
        const auto r = t.sign();
        CHECK(r.code == Code::missing_helper);
        CHECK(r.missingHelpers == std::vector<std::string> {"core.vbs", "helper.vbs"});
    }
    SECTION("user/ comes before scripts/ within a folder")
    {
        Tree t("user_before_scripts");
        t.put(t.table / "user" / "Helper.vbs", vpx_fixtures::kHelper);
        t.put(t.table / "scripts" / "Helper.vbs", "Sub Other() : End Sub\r\n");
        t.put(t.table / "extra.vbs", vpx_fixtures::kExtra);
        t.put(t.app / "core.vbs", "'");
        const auto r = t.sign();
        REQUIRE(r.code == Code::ok);
        CHECK(r.inputs.helperScripts.at(0).content == vpx_fixtures::kHelper);
    }
    SECTION("the install folder comes before the preferences folder")
    {
        // The sidecar shows which copy was read: a name-only helper would not.
        Tree t("app_before_pref");
        t.put(t.app / "scripts" / "table.vbs", vpx_fixtures::kSidecar);
        t.put(t.pref / "table.vbs", "Sub FromPref() : End Sub\r\n");
        const auto r = t.sign();
        REQUIRE(r.code == Code::ok);
        CHECK(r.inputs.script == vpx_fixtures::kSidecar);
    }
}

TEST_CASE("the sidecar is found by the script search, then by the folder's name", "[vpx_reader]")
{
    SECTION("table.vbs in the table's scripts/")
    {
        Tree t("sidecar_scripts");
        t.put(t.table / "scripts" / "Table.VBS", vpx_fixtures::kSidecar);
        const auto r = t.sign();
        REQUIRE(r.code == Code::ok);
        CHECK(r.sidecar);
        CHECK(r.inputs.script == vpx_fixtures::kSidecar);
    }
    SECTION("table.vbs in VPX's preferences folder")
    {
        Tree t("sidecar_pref");
        t.put(t.pref / "user" / "table.vbs", vpx_fixtures::kSidecar);
        const auto r = t.sign();
        REQUIRE(r.code == Code::ok);
        CHECK(r.sidecar);
        CHECK(r.inputs.script == vpx_fixtures::kSidecar);
    }
    SECTION("<folder>.vbs in the table's folder, after the search")
    {
        Tree t("sidecar_folder", "MyTable");
        t.put(t.table / "mytable.vbs", vpx_fixtures::kSidecar);
        auto r = t.sign();
        REQUIRE(r.code == Code::ok);
        CHECK(r.sidecar);
        CHECK(r.inputs.script == vpx_fixtures::kSidecar);

        t.put(t.app / "table.vbs", "Sub FromApp() : End Sub\r\n");
        r = t.sign();
        REQUIRE(r.code == Code::ok);
        CHECK(r.inputs.script == "Sub FromApp() : End Sub\r\n");
    }
}

TEST_CASE("helper names with folders are followed; ones that leave the search are not",
          "[vpx_reader]")
{
    auto model = vpx_fixtures::table();
    model.inputs.script = "ExecuteGlobal GetTextFile(\"lib\\Shared.vbs\")\r\n"
                          "ExecuteGlobal GetTextFile(\"..\\outside.vbs\")\r\n"
                          "ExecuteGlobal GetTextFile(\"/abs.vbs\")\r\n"
                          "ExecuteGlobal GetTextFile(\"C:\\drive.vbs\")\r\n";
    Tree t("relative");
    const auto bytes = vpx_fixtures::build(model).bytes;
    std::ofstream(t.table / "table.vpx", std::ios::binary | std::ios::trunc)
            .write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    t.put(t.table / "Lib" / "shared.vbs", "Sub Shared() : End Sub\r\n");
    t.put(t.root / "outside.vbs", "Sub Outside() : End Sub\r\n");
    const auto r = t.sign();
    CHECK(r.code == Code::missing_helper);
    CHECK(r.signature.top == Digest {}); // no signature
    CHECK(helperNames(r) == std::vector<std::string> {"lib/shared.vbs"});
    CHECK(r.missingHelpers
          == std::vector<std::string> {"..\\outside.vbs", "/abs.vbs", "c:\\drive.vbs"});
}

TEST_CASE("the embedded script ends at its first NUL", "[vpx_reader]")
{
    auto model = vpx_fixtures::table();
    model.inputs.script = std::string("Sub Real() : End Sub\r\n") + '\0' + "Sub Hidden() : End Sub\r\n";
    const auto bytes = vpx_fixtures::build(model).bytes;
    const auto c = vpx::parse(bytes.data(), bytes.size());
    REQUIRE(c.code == Code::ok);
    CHECK(c.inputs.script == "Sub Real() : End Sub\r\n");
}

TEST_CASE("a textbox's and a decal's FONT descriptor is read as the field's value", "[vpx_reader]")
{
    const auto model = vpx_fixtures::table();
    const auto c = parseFile(kFixtures / "unmodified" / "table.vpx");
    REQUIRE(c.code == Code::ok);
    REQUIRE(c.inputs.items.size() == 8);
    for (const size_t i : {size_t(6), size_t(7)}) {
        INFO(model.inputs.items[i].name);
        CHECK(c.inputs.items[i].name == model.inputs.items[i].name);
        requireSameFields(c.inputs.items[i].fields, model.inputs.items[i].fields);
    }
    CHECK(c.inputs.items[6].fields[2].value == vpx_fixtures::fontDescriptor("Arial", 120000));
}

TEST_CASE("vpx reader reports unreadable and malformed tables", "[vpx_reader]")
{
    for (const char *name : {"truncated.vpx", "bad_header.vpx", "fat_cycle.vpx", "bad_biff.vpx"}) {
        INFO(name);
        vpx::Paths p {(kFixtures / "negative" / name).string(), "", ""};
        CHECK(vpx::signFile(p).code == Code::malformed);
    }
    CHECK(vpx::signFile({(kFixtures / "no_such_table.vpx").string(), "", ""}).code
          == Code::table_unreadable);
    CHECK(vpx::signFile({kFixtures.string(), "", ""}).code == Code::table_unreadable);
    CHECK(vpx::parse(nullptr, 0).code == Code::malformed);

#ifndef _WIN32
    const fs::path dir = scratch("permission");
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path locked = dir / "table.vpx";
    fs::copy_file(kFixtures / "unmodified" / "table.vpx", locked);
    fs::permissions(locked, fs::perms::none);
    std::ifstream probe(locked);
    if (!probe.good()) { // running as root ignores the permission bits
        CHECK(vpx::signFile({locked.string(), "", ""}).code == Code::table_unreadable_permission);
    }
    fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_write);
    fs::remove_all(dir);
#endif
}

TEST_CASE("vpx reader caps sizes and counts", "[vpx_reader]")
{
    const fs::path table = kFixtures / "unmodified" / "table.vpx";
    auto with = [](auto set) {
        vpx::Options o;
        set(o.limits);
        return o;
    };
    CHECK(parseFile(table, with([](vpx::Limits &l) { l.maxFileSize = 1000; })).code
          == Code::signature_too_large);
    CHECK(parseFile(table, with([](vpx::Limits &l) { l.maxStreamSize = 1000; })).code
          == Code::signature_too_large);
    CHECK(parseFile(table, with([](vpx::Limits &l) { l.maxTotalRead = 5000; })).code
          == Code::signature_too_large);
    CHECK(parseFile(table, with([](vpx::Limits &l) { l.maxDirectoryEntries = 4; })).code
          == Code::signature_too_large);
    CHECK(parseFile(table, with([](vpx::Limits &l) { l.maxStreams = 3; })).code
          == Code::signature_too_large);
    CHECK(parseFile(table, with([](vpx::Limits &l) { l.maxRecords = 5; })).code
          == Code::signature_too_large);
    CHECK(parseFile(table, with([](vpx::Limits &l) { l.maxDragPoints = 3; })).code
          == Code::signature_too_large);
    CHECK(parseFile(table, with([](vpx::Limits &l) { l.maxMeshBytes = 100; })).code
          == Code::signature_too_large);
    CHECK(vpx::signFile(paths("unmodified"), with([](vpx::Limits &l) { l.maxHelperScripts = 1; }))
                  .code
          == Code::signature_too_large);
    CHECK(vpx::signFile(paths("unmodified"), with([](vpx::Limits &l) { l.maxTotalRead = 5600; }))
                  .code
          == Code::signature_too_large);

    std::atomic<bool> cancel {true};
    vpx::Options o;
    o.cancel = &cancel;
    CHECK(parseFile(table, o).code == Code::cancelled);
    CHECK(vpx::signFile(paths("unmodified"), o).code == Code::cancelled);
}

TEST_CASE("inflated meshes count toward the total read cap", "[vpx_reader]")
{
    // The smallest maxTotalRead a table parses under.
    auto smallest = [](const std::vector<uint8_t> &b) {
        uint64_t lo = 0, hi = 1u << 20;
        while (lo < hi) {
            const uint64_t mid = (lo + hi) / 2;
            vpx::Options o;
            o.limits.maxTotalRead = mid;
            if (vpx::parse(b.data(), b.size(), o).code == Code::ok) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    };
    auto model = vpx_fixtures::table();
    const auto withMesh = vpx_fixtures::build(model).bytes;
    for (auto &f : model.inputs.items[4].fields) {
        if (f.tag == "U3DM") {
            f.value = vpx_fixtures::i32(0); // same stream size, mesh not read
        }
    }
    const auto noMesh = vpx_fixtures::build(model).bytes;
    REQUIRE(withMesh.size() == noMesh.size());
    // 24 inflated vertices of 32 bytes and 36 16-bit indices, then the 24 x, y, z kept and the 36
    // indices kept as 32-bit.
    CHECK(smallest(withMesh) - smallest(noMesh) == 24 * 32 + 36 * 2 + 24 * 12 + 36 * 4);
}

TEST_CASE("parsed records count toward the total read cap", "[vpx_reader]")
{
    auto smallest = [](const std::vector<uint8_t> &b) {
        uint64_t lo = 0, hi = 1u << 24;
        while (lo < hi) {
            const uint64_t mid = (lo + hi) / 2;
            vpx::Options o;
            o.limits.maxTotalRead = mid;
            if (vpx::parse(b.data(), b.size(), o).code == Code::ok) {
                hi = mid;
            } else {
                lo = mid + 1;
            }
        }
        return lo;
    };
    auto model = vpx_fixtures::table();
    const auto plain = vpx_fixtures::build(model).bytes;
    for (int i = 0; i < 1000; i++) {
        model.inputs.items[0].fields.push_back({"ZZZZ", {}}); // 8 bytes in the file each
    }
    const auto many = vpx_fixtures::build(model).bytes;
    // Each tiny record costs its object as well as its 8 bytes, so 1000 of them cost far more
    // than the 8000 bytes they add to the file.
    CHECK(smallest(many) - smallest(plain) >= 1000 * (8 + vpx::detail::kParsedOverhead));
}

TEST_CASE("items and collections are read as VPX loads them", "[vpx_reader]")
{
    auto model = vpx_fixtures::table();
    for (int i = 0; i < 6; i++) { // 14 items, so GameItem10 sorts after GameItem9, not GameItem1
        auto wall = vpx_fixtures::named(kSurface, "Extra" + std::to_string(i));
        wall.fields.push_back({"HTTP", vpx_fixtures::f32(float(10 + i))});
        model.inputs.items.push_back(wall);
    }
    vpx_fixtures::setCounts(model.inputs);
    const auto streams = vpx_fixtures::streams(model);
    auto parseStreams = [](const std::vector<vpx_fixtures::Stream> &st) {
        const auto b = vpx_fixtures::writeCfb(st).bytes;
        return vpx::parse(b.data(), b.size());
    };

    const auto c = parseStreams(streams);
    REQUIRE(c.code == Code::ok);
    REQUIRE(c.inputs.items.size() == 14);
    for (size_t i = 0; i < 14; i++) {
        CHECK(c.inputs.items[i].name == model.inputs.items[i].name);
    }
    const auto expected = hex(compute(c.inputs).top);

    SECTION("streams past SEDT, or not named GameItemN exactly, are not read")
    {
        auto st = streams;
        st.push_back({"GameItem14", vpx_fixtures::itemStream(model.inputs.items[0])});
        st.push_back({"GameItem01", vpx_fixtures::itemStream(model.inputs.items[0])});
        st.push_back({"Collection1", vpx_fixtures::collectionStream(model.inputs.collections[0])});
        const auto r = parseStreams(st);
        REQUIRE(r.code == Code::ok);
        CHECK(hex(compute(r.inputs).top) == expected);
    }
    SECTION("a missing item or collection stream is skipped, as VPX skips it")
    {
        auto st = streams;
        st.erase(std::remove_if(st.begin(), st.end(), [](const auto &s) { return s.name == "GameItem5"; }),
                 st.end());
        const auto items = parseStreams(st);
        REQUIRE(items.code == Code::ok);
        REQUIRE(items.inputs.items.size() == 13);
        CHECK(items.inputs.items[5].name == model.inputs.items[6].name);
        st = streams;
        st.erase(std::remove_if(st.begin(), st.end(), [](const auto &s) { return s.name == "Collection0"; }),
                 st.end());
        const auto r = parseStreams(st);
        REQUIRE(r.code == Code::ok);
        CHECK(r.inputs.collections.empty());
    }
    SECTION("two streams whose names differ only in case are malformed")
    {
        auto st = streams;
        st.push_back({"gameitem3", vpx_fixtures::itemStream(model.inputs.items[0])});
        CHECK(parseStreams(st).code == Code::malformed);
    }
}

TEST_CASE("scripts are capped in bytes and tokens", "[vpx_reader]")
{
    vpx::Options o;
    o.limits.maxScriptBytes = 100;
    CHECK(vpx::signFile(paths("unmodified"), o).code == Code::signature_too_large);
    o = {};
    o.limits.maxScriptTokens = 20;
    CHECK(vpx::signFile(paths("unmodified"), o).code == Code::signature_too_large);
    CHECK(vpx::signFile(paths("unmodified")).code == Code::ok);
}

TEST_CASE("helper names with more than eight parts are not followed", "[vpx_reader]")
{
    auto model = vpx_fixtures::table();
    model.inputs.script = "ExecuteGlobal GetTextFile(\"a/b/c/d/e/f/g/eight.vbs\")\r\n"
                          "ExecuteGlobal GetTextFile(\"a/b/c/d/e/f/g/h/nine.vbs\")\r\n";
    Tree t("parts");
    const auto bytes = vpx_fixtures::build(model).bytes;
    std::ofstream(t.table / "table.vpx", std::ios::binary | std::ios::trunc)
            .write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
    t.put(t.table / "a/b/c/d/e/f/g/eight.vbs", "'");
    t.put(t.table / "a/b/c/d/e/f/g/h/nine.vbs", "'");
    const auto r = t.sign();
    CHECK(helperNames(r) == std::vector<std::string> {"a/b/c/d/e/f/g/eight.vbs"});
    CHECK(r.missingHelpers == std::vector<std::string> {"a/b/c/d/e/f/g/h/nine.vbs"});
}

#ifndef _WIN32
TEST_CASE("the script search never follows a link or opens a FIFO", "[vpx_reader]")
{
    SECTION("a helper that is a symbolic link")
    {
        Tree t("link_file");
        t.put(t.root / "elsewhere" / "secret.txt", "Sub Secret() : End Sub\r\n");
        fs::create_symlink(t.root / "elsewhere" / "secret.txt", t.table / "Helper.vbs");
        t.put(t.app / "core.vbs", "'");
        const auto r = t.sign();
        CHECK(r.code == Code::missing_helper);
        CHECK(r.missingHelpers == std::vector<std::string> {"helper.vbs"});
        CHECK(r.inputs.helperScripts.empty());
    }
    SECTION("a folder under the table's that is a symbolic link")
    {
        Tree t("link_dir");
        t.put(t.root / "elsewhere" / "Helper.vbs", vpx_fixtures::kHelper);
        fs::create_directory_symlink(t.root / "elsewhere", t.table / "user");
        t.put(t.app / "core.vbs", "'");
        const auto r = t.sign();
        CHECK(r.code == Code::missing_helper);
        CHECK(r.missingHelpers == std::vector<std::string> {"helper.vbs"});
    }
    SECTION("VPX's own folders are followed through links")
    {
        for (const char *where : {"app/scripts", "pref/user"}) {
            INFO(where);
            Tree t("link_vpx");
            t.put(t.table / "Helper.vbs", vpx_fixtures::kHelper);
            t.put(t.table / "extra.vbs", vpx_fixtures::kExtra);
            t.put(t.root / "shared" / "core.vbs", "'");
            fs::create_directory_symlink(t.root / "shared", t.root / where);
            const auto r = t.sign();
            REQUIRE(r.code == Code::ok);
            CHECK(r.inputs.vpxScripts == std::vector<std::string> {"core.vbs"});
        }
    }
    SECTION("a sidecar that is a FIFO")
    {
        Tree t("fifo");
        REQUIRE(::mkfifo((t.table / "table.vbs").c_str(), 0600) == 0);
        t.put(t.table / "Helper.vbs", vpx_fixtures::kHelper);
        t.put(t.table / "extra.vbs", vpx_fixtures::kExtra);
        t.put(t.app / "core.vbs", "'");
        const auto r = t.sign(); // returns: the FIFO is never opened
        REQUIRE(r.code == Code::ok);
        CHECK_FALSE(r.sidecar);
    }
}
#endif

TEST_CASE("result codes keep their values", "[vpx_reader]")
{
    CHECK(int(Code::ok) == 0);
    CHECK(int(Code::table_unreadable_permission) == 1);
    CHECK(int(Code::table_unreadable) == 2);
    CHECK(int(Code::legacy_protected) == 3);
    CHECK(int(Code::unnamed_helper) == 4);
    CHECK(int(Code::missing_helper) == 5);
    CHECK(int(Code::signature_too_large) == 6);
    CHECK(int(Code::malformed) == 7);
    CHECK(int(Code::cancelled) == 8);
}

TEST_CASE("vpx reader survives the malformed corpus", "[vpx_reader]")
{
    const auto cases = vpx_fixtures::corpus();
    REQUIRE(cases.size() > 500);
    for (const auto &c : cases) {
        INFO(c.name);
        const auto r = vpx::parse(c.bytes.data(), c.bytes.size());
        CHECK(int(r.code) >= int(Code::ok));
        CHECK(int(r.code) <= int(Code::cancelled));
        if (c.expect >= 0) {
            CHECK(vpx::codeName(r.code) == std::string(vpx::codeName(Code(c.expect))));
        }
        if (r.code == Code::ok) {
            (void)compute(r.inputs); // whatever parsed must also hash
        }
    }
}
