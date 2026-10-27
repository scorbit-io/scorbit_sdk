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
        in.items.push_back({i.at("type").get<uint32_t>(), i.at("name").get<std::string>(), fields(i.at("fields"))});
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

TEST_CASE("table_signature: an item that loses an allow-listed field is not the same as one at its default")
{
    Item with {kFlipper, "F", {{"STRG", {0, 0, 0, 0}}}};
    Item without {kFlipper, "F", {}};
    CHECK(itemsDigest({with}) != itemsDigest({without}));
    Item renamed {kFlipper, "G", {{"STRG", {0, 0, 0, 0}}}};
    CHECK(itemsDigest({with}) != itemsDigest({renamed}));
}
