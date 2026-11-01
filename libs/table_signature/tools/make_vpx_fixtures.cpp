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

// Generates the synthetic .vpx fixtures under fixtures/vpx/ and their expected signatures in
// vpx_fixtures.json. Expected digests come from compute() over the model the file was written
// from, not from the reader, so the tests check the reader against an independent answer.
//
//   c++ -std=c++17 -I include -I tools tools/make_vpx_fixtures.cpp -o make_vpx_fixtures
//   ./make_vpx_fixtures .                 # run in libs/table_signature
//   ./make_vpx_fixtures . --corpus DIR    # also write the malformed corpus, one file per case

#include "vpx_writer.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace vpx_fixtures;
namespace fs = std::filesystem;

static void writeFile(const fs::path &p, const std::string &data)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(data.data(), std::streamsize(data.size()));
    if (!f) {
        std::fprintf(stderr, "cannot write %s\n", p.string().c_str());
        std::exit(1);
    }
}

static void writeFile(const fs::path &p, const Bytes &data)
{
    writeFile(p, std::string(data.begin(), data.end()));
}

static auto quote(const std::string &s) -> std::string
{
    return "\"" + s + "\"";
}

int main(int argc, char **argv)
{
    if (argc != 2 && !(argc == 4 && std::string(argv[2]) == "--corpus")) {
        std::fprintf(stderr, "usage: %s <libs/table_signature> [--corpus DIR]\n", argv[0]);
        return 2;
    }
    const fs::path root = argv[1];
    const fs::path dir = root / "fixtures" / "vpx";
    fs::remove_all(dir);
    writeFile(dir / "app" / "scripts" / "core.vbs", std::string("' a script shipped with VPX\r\n"));

    struct Fixture {
        std::string name, note;
        Table table;
        bool helpers = true;
        std::string sidecar;
        vpx::Code code = vpx::Code::ok;
    };
    auto fixture = [](const std::string &name, const std::string &note, const Table &t) {
        Fixture f;
        f.name = name;
        f.note = note;
        f.table = t;
        return f;
    };
    std::vector<Fixture> fixtures;
    fixtures.push_back(fixture("unmodified", "the reference table", table()));
    fixtures.push_back(fixture("cosmetic",
                               "script reformatted, images, colours, an unknown record and the "
                               "table name changed: same signature",
                               table(true)));
    fixtures.push_back(fixture("moved_bumper", "the bumper moves 40 units: items digest changes",
                               table(false, 640.f)));
    {
        Fixture f = fixture("dynamic_helper",
                            "the script loads a helper by a variable name: no signature", table());
        f.table.inputs.script += "Dim f : f = \"Helper.vbs\" : LoadScript f\r\n";
        f.code = vpx::Code::unnamed_helper;
        fixtures.push_back(f);
    }
    {
        Fixture f = fixture("protected", "legacy table protection with script editing disabled",
                            table());
        f.table.inputs.tableFields.push_back(protectionRecord());
        f.code = vpx::Code::legacy_protected;
        fixtures.push_back(f);
    }
    {
        Fixture f = fixture("missing_helper",
                            "Helper.vbs is found nowhere: VPX would fail to load it, no signature",
                            table());
        f.helpers = false;
        f.code = vpx::Code::missing_helper;
        fixtures.push_back(f);
    }
    {
        Fixture f = fixture("sidecar", "table.vbs beside the table replaces the embedded script",
                            table());
        f.helpers = false;
        f.sidecar = kSidecar;
        fixtures.push_back(f);
    }

    std::string json = "[\n";
    for (size_t i = 0; i < fixtures.size(); i++) {
        const Fixture &f = fixtures[i];
        const fs::path d = dir / f.name;
        writeFile(d / "table.vpx", build(f.table).bytes);
        Inputs expected = f.table.inputs;
        std::vector<std::string> missing;
        if (!f.sidecar.empty()) {
            writeFile(d / "table.vbs", f.sidecar);
            expected.script = f.sidecar;
        } else {
            expected.vpxScripts = {"core.vbs"};
            if (f.helpers) {
                writeFile(d / "Helper.vbs", std::string(kHelper));
                writeFile(d / "extra.vbs", std::string(kExtra));
                expected.helperScripts = {{"helper.vbs", kHelper}, {"extra.vbs", kExtra}};
            } else {
                missing = {"helper.vbs"};
            }
        }
        std::string top, script, items, physics;
        if (f.code != vpx::Code::legacy_protected && f.code != vpx::Code::missing_helper) {
            const Signature s = compute(expected);
            if (s.unavailable.empty() != (f.code == vpx::Code::ok)) {
                std::fprintf(stderr, "%s: compute disagrees with the expected code\n",
                             f.name.c_str());
                return 1;
            }
            if (s.unavailable.empty()) {
                top = hex(s.top);
                script = hex(s.script);
                items = hex(s.items);
                physics = hex(s.physics);
            }
        }
        std::string missingJson;
        for (const auto &m : missing) {
            missingJson += (missingJson.empty() ? "" : ",") + quote(m);
        }
        json += "  {\"name\":" + quote(f.name) + ",\"note\":" + quote(f.note) + ",\"code\":"
              + quote(vpx::codeName(f.code)) + ",\"missingHelpers\":[" + missingJson + "],\"top\":"
              + quote(top) + ",\"script\":" + quote(script) + ",\"items\":" + quote(items)
              + ",\"physics\":" + quote(physics) + "}" + (i + 1 < fixtures.size() ? ",\n" : "\n");
    }
    json += "]\n";
    writeFile(root / "vpx_fixtures.json", json);

    // A few malformed files on disk, for the path entry point.
    const Bytes valid = build(table()).bytes;
    writeFile(dir / "negative" / "truncated.vpx",
              Bytes(valid.begin(), valid.begin() + long(valid.size() / 2)));
    writeFile(dir / "negative" / "bad_header.vpx", Bytes(valid.begin() + 8, valid.end()));
    for (const auto &c : corpus()) {
        if (c.name == "stream chain cycle") {
            writeFile(dir / "negative" / "fat_cycle.vpx", c.bytes);
        } else if (c.name == "record length past the stream") {
            writeFile(dir / "negative" / "bad_biff.vpx", c.bytes);
        }
    }

    if (argc == 4) {
        size_t n = 0;
        for (const auto &c : corpus()) {
            char name[32];
            std::snprintf(name, sizeof name, "case%04zu.vpx", n++);
            writeFile(fs::path(argv[3]) / name, c.bytes);
        }
    }
    return 0;
}
