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

#include <table_signature/table_signature.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <fstream>

using namespace table_signature;

namespace {

auto unhex(const std::string &s) -> std::vector<uint8_t>
{
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoi(s.substr(i, 2), nullptr, 16)));
    }
    return out;
}

auto fields(const nlohmann::json &j) -> std::vector<Field>
{
    std::vector<Field> out;
    for (const auto &f : j) {
        out.push_back({f.at("tag").get<std::string>(), unhex(f.at("hex").get<std::string>())});
    }
    return out;
}

auto inputs(const nlohmann::json &j) -> Inputs
{
    Inputs in;
    in.script = j.at("script").get<std::string>();
    for (const auto &h : j.at("helperScripts")) {
        in.helperScripts.push_back({h.at("name").get<std::string>(), h.at("content").get<std::string>()});
    }
    in.vpxScripts = j.at("vpxScripts").get<std::vector<std::string>>();
    in.dynamicLoads = j.at("dynamicLoads").get<uint32_t>();
    for (const auto &i : j.at("items")) {
        Item item {i.at("type").get<uint32_t>(), i.at("name").get<std::string>(), fields(i.at("fields"))};
        for (const auto &p : i.value("dragPoints", nlohmann::json::array())) {
            item.dragPoints.push_back(fields(p));
        }
        if (i.contains("mesh")) {
            item.mesh.positions = i.at("mesh").at("positions").get<std::vector<float>>();
            item.mesh.indices = i.at("mesh").at("indices").get<std::vector<uint32_t>>();
        }
        in.items.push_back(std::move(item));
    }
    for (const auto &c : j.value("collections", nlohmann::json::array())) {
        in.collections.push_back({c.at("name").get<std::string>(), c.at("members").get<std::vector<std::string>>(),
                                  fields(c.at("fields"))});
    }
    in.tableFields = fields(j.at("tableFields"));
    for (const auto &m : j.at("materials")) {
        in.materials.push_back({m.at("name").get<std::string>(), m.at("elasticity").get<float>(),
                                m.at("elasticityFalloff").get<float>(), m.at("friction").get<float>(),
                                m.at("scatterAngle").get<float>()});
    }
    return in;
}

auto vectors() -> nlohmann::json
{
    std::ifstream f(TABLE_SIGNATURE_VECTORS);
    REQUIRE(f.good());
    return nlohmann::json::parse(f);
}

auto byName(const nlohmann::json &v, const std::string &name) -> Signature
{
    for (const auto &c : v) {
        if (c.at("name") == name) {
            return compute(inputs(c.at("inputs")));
        }
    }
    FAIL("no vector " << name);
    return {};
}

auto procedure(const Signature &s, const std::string &key) -> std::string
{
    for (const auto &p : s.procedures) {
        if (p.key == key) {
            return hex(p.digest.data(), p.digest.size());
        }
    }
    return {};
}

} // namespace

TEST_CASE("table_signature: SHA-256 known answers")
{
    Sha256 empty;
    CHECK(hex(empty.finish()) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Sha256 abc;
    abc.update("abc");
    CHECK(hex(abc.finish()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    Sha256 two;
    two.update("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    CHECK(hex(two.finish()) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("table_signature: every vector recomputes to its expected digests")
{
    for (const auto &c : vectors()) {
        INFO(c.at("name").get<std::string>());
        const auto s = compute(inputs(c.at("inputs")));
        const auto &e = c.at("expected");
        CHECK(e.at("version") == s.version);
        CHECK(e.at("digest") == hex(s.top));
        CHECK(e.at("sections").at("script") == hex(s.script));
        CHECK(e.at("sections").at("items") == hex(s.items));
        CHECK(e.at("sections").at("physics") == hex(s.physics));
        REQUIRE(e.at("procedures").size() == s.procedures.size());
        for (size_t i = 0; i < s.procedures.size(); i++) {
            CHECK(e.at("procedures")[i][0] == s.procedures[i].key);
            CHECK(e.at("procedures")[i][1] == hex(s.procedures[i].digest.data(), 8));
        }
    }
}

TEST_CASE("table_signature: a cosmetic edit keeps the signature and each rule edit changes only its section")
{
    const auto v = vectors();
    const auto base = byName(v, "unmodified");
    CHECK(byName(v, "cosmetic").top == base.top);

    const auto scoring = byName(v, "scoring_edit");
    CHECK(scoring.script != base.script);
    CHECK(scoring.items == base.items);
    CHECK(scoring.physics == base.physics);
    CHECK(procedure(scoring, "bumper1_hit") != procedure(base, "bumper1_hit"));
    CHECK(procedure(scoring, "addscore") == procedure(base, "addscore"));

    for (const char *name : {"timer_edit", "flipper_edit"}) {
        const auto s = byName(v, name);
        CHECK(s.items != base.items);
        CHECK(s.script == base.script);
        CHECK(s.physics == base.physics);
    }
    const auto slope = byName(v, "slope_edit");
    CHECK(slope.physics != base.physics);
    CHECK(slope.items == base.items);

    const auto helper = byName(v, "helper_edit");
    CHECK(helper.script != base.script);
    CHECK(procedure(helper, "rules.vbs:enableballsaver") != procedure(base, "rules.vbs:enableballsaver"));
    CHECK(procedure(helper, "bumper1_hit") == procedure(base, "bumper1_hit"));

    const auto lineIf = byName(v, "line_if_edit");
    CHECK(lineIf.script != base.script);
    CHECK(procedure(lineIf, "") != procedure(base, ""));
    CHECK(lineIf.items == base.items);

    for (const char *name : {"bumper_moved", "wall_reshaped", "collection_edit"}) {
        const auto s = byName(v, name);
        CHECK(s.items != base.items);
        CHECK(s.script == base.script);
    }
}

TEST_CASE("table_signature: string literals keep their case, identifiers do not")
{
    CHECK(canonicalScript(tokenize("PlaySound \"Bumper\"")) == canonicalScript(tokenize("playsound \"Bumper\"")));
    CHECK(canonicalScript(tokenize("PlaySound \"Bumper\"")) != canonicalScript(tokenize("PlaySound \"bumper\"")));
    CHECK(canonicalScript(tokenize("x = 1 ' set x")) == canonicalScript(tokenize("X=1")));
    CHECK(canonicalScript(tokenize("a = 1 : b = 2")) == canonicalScript(tokenize("a = 1\r\n\r\nb = 2")));
    CHECK(canonicalScript(tokenize("Rem note\nx = 1")) == canonicalScript(tokenize("x = 1")));
    CHECK(canonicalScript(tokenize("x = &HFF")) == canonicalScript(tokenize("x = &hff")));
    CHECK(canonicalScript(tokenize("x = \"a\"\"b\"")) != canonicalScript(tokenize("x = \"a\"\"B\"")));
}

TEST_CASE("table_signature: a Latin-1 script hashes as its UTF-8 re-save")
{
    const std::string latin1 = "Msg = \"caf\xe9\"";
    const std::string utf8 = "Msg = \"caf\xc3\xa9\"";
    CHECK(decodeScript(latin1) == utf8);
    CHECK(decodeScript(utf8) == utf8);
}

TEST_CASE("table_signature: script references list literal loads and count the rest")
{
    const auto refs = scriptReferences(tokenize(
            "ExecuteGlobal GetTextFile(\"Core.vbs\")\nLoadScript \"rules.vbs\"\n"
            "LoadScript(\"rules.vbs\")\nExecuteGlobal GetTextFile(name & \".vbs\")"));
    CHECK(refs.names == std::vector<std::string> {"core.vbs", "rules.vbs"});
    CHECK(refs.dynamic == 1);
}

TEST_CASE("table_signature: a colon inside a single-line If is not a line break")
{
    // Both assignments are conditional here; split by a newline, the second is not
    CHECK(canonicalScript(tokenize("If False Then score = 1 : bonus = 2"))
          != canonicalScript(tokenize("If False Then score = 1\nbonus = 2")));
    CHECK(canonicalScript(tokenize("If award Then _\n  score = 1 : balls = 2"))
          != canonicalScript(tokenize("If award Then score = 1\nballs = 2")));
    // A block If and its statements keep the ordinary separator
    CHECK(canonicalScript(tokenize("If x Then\n a = 1 : b = 2\nEnd If"))
          == canonicalScript(tokenize("If x Then ' note\n a = 1\n b = 2\nEnd If")));
}

TEST_CASE("table_signature: a script's own Sub LoadScript is a declaration, not a load")
{
    const auto refs = scriptReferences(tokenize("Sub LoadScript(name)\n  x = name\nEnd Sub\nLoadScript \"a.vbs\""));
    CHECK(refs.dynamic == 0);
    CHECK(refs.names == std::vector<std::string> {"a.vbs"});
    CHECK(scriptReferences(tokenize("Function GetTextFile(p) : GetTextFile = \"\" : End Function")).dynamic == 0);
    // A call with a name that is not a literal still counts
    CHECK(scriptReferences(tokenize("Sub LoadScript(n)\nEnd Sub\nLoadScript helper")).dynamic == 1);
}

TEST_CASE("table_signature: a literal load before Else is still listed")
{
    const auto refs = scriptReferences(
            tokenize("If useRules Then LoadScript \"rules.vbs\" Else LoadScript \"fallback.vbs\""));
    CHECK(refs.names == std::vector<std::string> {"rules.vbs", "fallback.vbs"});
    CHECK(refs.dynamic == 0);
    const auto colon = scriptReferences(tokenize("If a Then LoadScript \"a.vbs\" : LoadScript \"b.vbs\""));
    CHECK(colon.names == std::vector<std::string> {"a.vbs", "b.vbs"});
}

TEST_CASE("table_signature: a script that loads a helper it does not name has no signature")
{
    Inputs in;
    in.script = "helper = \"rules.vbs\"\nExecuteGlobal GetTextFile(helper)";
    in.dynamicLoads = scriptReferences(tokenize(in.script)).dynamic;
    REQUIRE(in.dynamicLoads == 1);
    const auto s = compute(in);
    CHECK(s.unavailable == kUnnamedHelper);
    CHECK(s.top == Digest {});
    CHECK(s.procedures.empty());
    // Found in the script itself, whatever the caller counted
    in.dynamicLoads = 0;
    CHECK(compute(in).unavailable == kUnnamedHelper);
    // ...and in a helper the script names
    Inputs nested;
    nested.script = "LoadScript \"rules.vbs\"";
    nested.helperScripts = {{"rules.vbs", "ExecuteGlobal GetTextFile(extra)"}};
    CHECK(compute(nested).unavailable == kUnnamedHelper);
    nested.helperScripts[0].content = "x = 1";
    CHECK(compute(nested).unavailable.empty());
}

TEST_CASE("table_signature: a legacy script is Windows-1252, and a BOM is dropped")
{
    CHECK(decodeScript("x = \"\x80\"") == "x = \"\xe2\x82\xac\""); // the euro sign
    CHECK(decodeScript("x = \"\x81\"") == "x = \"\xc2\x81\"");     // unassigned: kept as U+0081
    CHECK(decodeScript("\xef\xbb\xbfx = 1") == "x = 1");
}

TEST_CASE("table_signature: collection membership and order are part of the items section")
{
    const Item t1 {kHitTarget, "T1", {}, {}, {}}, t2 {kHitTarget, "T2", {}, {}, {}};
    const Collection both {"DropTargets", {"T1", "T2"}, {{"EVNT", {1, 0, 0, 0}}}};
    Collection fewer = both;
    fewer.members = {"T1"};
    CHECK(itemsDigest({t1, t2}, {both}) != itemsDigest({t1, t2}, {fewer}));
    Collection swapped = both;
    swapped.members = {"T2", "T1"}; // the index each member's events receive changes
    CHECK(itemsDigest({t1, t2}, {both}) != itemsDigest({t1, t2}, {swapped}));
    Collection recased = both;
    recased.name = "droptargets";
    recased.members = {"t1", "T2"};
    CHECK(itemsDigest({t1, t2}, {both}) == itemsDigest({t1, t2}, {recased}));
    Collection noEvents = both;
    noEvents.fields[0].value = {0, 0, 0, 0};
    CHECK(itemsDigest({t1, t2}, {both}) != itemsDigest({t1, t2}, {noEvents}));
}

TEST_CASE("table_signature: a repeated field takes its last value, as VPX applies it")
{
    const std::vector<uint8_t> a {1, 0, 0, 0}, b {2, 0, 0, 0};
    const Item last {kFlipper, "F", {{"STRG", a}, {"STRG", b}}, {}, {}};
    const Item onlyB {kFlipper, "F", {{"STRG", b}}, {}, {}};
    const Item onlyA {kFlipper, "F", {{"STRG", a}}, {}, {}};
    CHECK(itemsDigest({last}) == itemsDigest({onlyB}));
    CHECK(itemsDigest({last}) != itemsDigest({onlyA}));
}

TEST_CASE("table_signature: &H and &O take only their own digits")
{
    CHECK(tokenize("x = a &hxyz") == tokenize("x = a & hxyz"));
    CHECK(tokenize("x = &o78") == std::vector<std::string> {"x", "=", "&o7", "8"});
    CHECK(tokenize("x = &h1F&") == std::vector<std::string> {"x", "=", "&h1f&"});
}

TEST_CASE("table_signature: ill-formed UTF-8 is read as Latin-1")
{
    // C0 AF is an overlong '/', ED A0 80 a surrogate, F4 90 80 80 above U+10FFFF
    for (const std::string bad : {"x\xc0\xaf", "x\xed\xa0\x80", "x\xf4\x90\x80\x80"}) {
        INFO(hex(reinterpret_cast<const uint8_t *>(bad.data()), bad.size()));
        CHECK(decodeScript(bad) != bad);
    }
    CHECK(decodeScript("x\xc0\xa0") == "x\xc3\x80\xc2\xa0");
    const std::string good = "x\xe2\x82\xac\xf0\x9f\x8e\xb1"; // the euro sign and an 8-ball
    CHECK(decodeScript(good) == good);
}

TEST_CASE("table_signature: moving or resizing a collision shape changes the items section")
{
    auto f32x2 = [](float x, float y) {
        std::vector<uint8_t> v(8);
        std::memcpy(v.data(), &x, 4);
        std::memcpy(v.data() + 4, &y, 4);
        return v;
    };
    auto name = [](const std::string &s) {
        std::vector<uint8_t> v {static_cast<uint8_t>(s.size()), 0, 0, 0};
        v.insert(v.end(), s.begin(), s.end());
        return v;
    };
    const Item bumper {kBumper, "B", {{"VCEN", f32x2(100, 200)}, {"SURF", name("Wall1")}}};
    Item moved = bumper;
    moved.fields[0].value = f32x2(100, 260);
    CHECK(itemsDigest({bumper}) != itemsDigest({moved}));
    // SURF is looked up ignoring case, so only its case changing is not a change
    Item recased = bumper;
    recased.fields[1].value = name("WALL1");
    CHECK(itemsDigest({bumper}) == itemsDigest({recased}));
    Item retargeted = bumper;
    retargeted.fields[1].value = name("Wall2");
    CHECK(itemsDigest({bumper}) != itemsDigest({retargeted}));

    // VPOS keeps x, y, z only
    std::vector<uint8_t> pos(16, 0);
    Item prim {kPrimitive, "P", {{"VPOS", pos}}};
    Item junkW = prim;
    junkW.fields[0].value[12] = 0x7f;
    CHECK(itemsDigest({prim}) == itemsDigest({junkW}));
}

TEST_CASE("table_signature: drag points and a primitive's mesh shape collision")
{
    auto point = [](float x, float y, int32_t sling) {
        std::vector<uint8_t> vcen(8), slng(4);
        std::memcpy(vcen.data(), &x, 4);
        std::memcpy(vcen.data() + 4, &y, 4);
        std::memcpy(slng.data(), &sling, 4);
        return DragPoint {{"VCEN", vcen}, {"SLNG", slng}, {"TEXC", {1, 2, 3, 4}}};
    };
    Item wall {kSurface, "W", {}, {point(0, 0, 0), point(10, 0, 0), point(10, 10, 0)}};
    Item reordered = wall;
    std::swap(reordered.dragPoints[1], reordered.dragPoints[2]);
    CHECK(itemsDigest({wall}) != itemsDigest({reordered}));
    Item sling = wall;
    sling.dragPoints[1] = point(10, 0, 1);
    CHECK(itemsDigest({wall}) != itemsDigest({sling}));
    Item retextured = wall;
    retextured.dragPoints[1][2].value = {9, 9, 9, 9}; // TEXC: visual only
    CHECK(itemsDigest({wall}) == itemsDigest({retextured}));

    Item prim {kPrimitive, "P"};
    prim.mesh = {{0, 0, 0, 1, 0, 0, 0, 1, 0}, {0, 1, 2}};
    Item reshaped = prim;
    reshaped.mesh.positions[3] = 2;
    CHECK(itemsDigest({prim}) != itemsDigest({reshaped}));
    Item rewound = prim;
    rewound.mesh.indices = {0, 2, 1};
    CHECK(itemsDigest({prim}) != itemsDigest({rewound}));
}

TEST_CASE("table_signature: an item that loses an allow-listed field is not the same as one at its default")
{
    Item with {kFlipper, "F", {{"STRG", {0, 0, 0, 0}}}};
    Item without {kFlipper, "F", {}};
    CHECK(itemsDigest({with}) != itemsDigest({without}));
    Item renamed {kFlipper, "G", {{"STRG", {0, 0, 0, 0}}}};
    CHECK(itemsDigest({with}) != itemsDigest({renamed}));
}
