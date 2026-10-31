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

#include <atomic>
#include <filesystem>
#include <fstream>

using namespace table_signature;
using vpx::Code;
namespace fs = std::filesystem;

namespace {

const fs::path kRoot = TABLE_SIGNATURE_VPX_FIXTURES;
const fs::path kFixtures = kRoot / "fixtures" / "vpx";

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
        : root(fs::temp_directory_path() / ("vpx_reader_" + name))
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
    const fs::path dir = fs::temp_directory_path() / "vpx_reader_permission_test";
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
    // 24 vertices of 32 bytes and 36 16-bit indices.
    CHECK(smallest(withMesh) - smallest(noMesh) == 24 * 32 + 36 * 2);
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
