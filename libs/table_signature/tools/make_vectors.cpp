// Generates vectors.json for table_signature.h (vts1). Inputs are synthetic but shaped like a real
// table: a script with procedures, a helper script, items with allow-listed and visual fields, drag
// points and a primitive mesh.
#include <table_signature/table_signature.h>

#include <cstdio>
#include <string>
#include <vector>

using namespace table_signature;

static std::vector<uint8_t> i32(int32_t v) { return {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)}; }
static std::vector<uint8_t> f32(float f) { uint32_t b; std::memcpy(&b, &f, 4); return i32(int32_t(b)); }
static std::vector<uint8_t> cat(std::vector<uint8_t> a, const std::vector<uint8_t> &b) { a.insert(a.end(), b.begin(), b.end()); return a; }
static std::vector<uint8_t> vec2(float x, float y) { return cat(f32(x), f32(y)); }
static std::vector<uint8_t> vec4(float x, float y, float z) { return cat(cat(vec2(x, y), f32(z)), f32(0.f)); }
static std::vector<uint8_t> str(const std::string &t) { return cat(i32(int32_t(t.size())), std::vector<uint8_t>(t.begin(), t.end())); }
static DragPoint dp(float x, float y) { return {{"VCEN", vec2(x, y)}, {"SMTH", i32(0)}, {"SLNG", i32(0)}, {"TEXC", f32(0.f)}}; }

static const char *kScript =
    "Option Explicit\r\n"
    "' Scoring\r\n"
    "Dim Score : Score = 0\r\n"
    "ExecuteGlobal GetTextFile(\"core.vbs\")\r\n"
    "LoadScript \"rules.vbs\"\r\n"
    "Sub Bumper1_Hit()\r\n"
    "    AddScore 1000\r\n"
    "    PlaySound \"Bumper\"\r\n"
    "End Sub\r\n"
    "Sub AddScore(points)\r\n"
    "    Score = Score + points\r\n"
    "End Sub\r\n"
    "Function BallsPerGame() : BallsPerGame = 3 : End Function\r\n"
    "If Score > 99999 Then Score = 0 : Bonus = 1\r\n";

static const char *kCosmetic =
    "option explicit\n"
    "\n"
    "REM scoring, reformatted\n"
    "dim score\n"
    "score = 0   ' start at zero\n"
    "ExecuteGlobal GetTextFile(\"core.vbs\")\n"
    "LoadScript \"rules.vbs\"\n"
    "sub bumper1_hit()\n"
    "\tAddScore _\n"
    "\t    1000\n"
    "\tPlaySound \"Bumper\"\n"
    "end sub\n"
    "\n"
    "Sub ADDSCORE(Points)\n"
    "  Score = Score + Points\n"
    "End Sub\n"
    "Function BallsPerGame()\n"
    "  BallsPerGame = 3\n"
    "End Function\n"
    "if score > 99999 then score = 0:bonus = 1 ' roll over\n";

static Inputs base()
{
    Inputs in;
    in.script = kScript;
    in.helperScripts = {{"rules.vbs", "Sub EnableBallSaver() : BallSaver = True : End Sub\r\n"}};
    in.vpxScripts = {"core.vbs"};
    Item flipper {kFlipper, "LeftFlipper", {{"STRG", f32(2200.f)}, {"ELAS", f32(0.8f)}, {"VSBL", i32(1)}, {"MATR", {'M', 'e', 't', 'a', 'l'}}}, {}, {}};
    Item bumper {kBumper, "Bumper1", {{"FORC", f32(12.f)}, {"THRS", f32(1.f)}, {"HAHE", i32(1)}, {"COLI", i32(1)},
                                         {"VCEN", vec2(450.f, 600.f)}, {"SURF", str("Wall1")}}, {}, {}};
    Item timer {2, "BallSaveTimer", {{"TMON", i32(0)}, {"TMIN", i32(8000)}}, {}, {}};
    Item wall {kSurface, "Wall1", {{"HTTP", f32(50.f)}}, {dp(400.f, 550.f), dp(500.f, 550.f), dp(500.f, 650.f)}, {}};
    Item post {kPrimitive, "Post", {{"VPOS", vec4(300.f, 400.f, 0.f)}, {"U3DM", i32(1)}}, {},
               {{0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 0.f, 10.f, 0.f, 0.f, 0.f, 30.f}, {0, 1, 2, 0, 1, 3}}};
    in.items = {flipper, bumper, timer, wall, post};
    in.tableFields = {{"GAVT", f32(1.762f)}, {"SLOP", f32(6.f)}, {"SLPX", f32(6.f)}, {"TDFT", f32(0.2f)}, {"OFFX", f32(12.f)}, {"ZOOM", f32(0.5f)}};
    in.materials = {{"Rubber", 0.9f, 0.3f, 0.6f, 5.f}, {"Metal", 0.3f, 0.f, 0.15f, 0.f}};
    return in;
}

static void field(std::string &o, const Field &f)
{
    o += "{\"tag\":\"" + f.tag + "\",\"hex\":\"" + hex(f.value.data(), f.value.size()) + "\"}";
}

static std::string esc(const std::string &s)
{
    std::string o;
    for (const char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    return o;
}

static void emit(std::string &o, const char *name, const char *note, const Inputs &in, bool last)
{
    const auto s = compute(in);
    o += "  {\"name\":\"" + std::string(name) + "\",\"note\":\"" + note + "\",\"inputs\":{";
    o += "\"script\":\"" + esc(in.script) + "\",\"helperScripts\":[";
    for (size_t i = 0; i < in.helperScripts.size(); i++) {
        o += (i ? "," : "") + std::string("{\"name\":\"") + esc(in.helperScripts[i].name) + "\",\"content\":\"" + esc(in.helperScripts[i].content) + "\"}";
    }
    o += "],\"vpxScripts\":[";
    for (size_t i = 0; i < in.vpxScripts.size(); i++) o += (i ? "," : "") + std::string("\"") + in.vpxScripts[i] + "\"";
    o += "],\"dynamicLoads\":" + std::to_string(in.dynamicLoads) + ",\"items\":[";
    for (size_t i = 0; i < in.items.size(); i++) {
        o += (i ? "," : "") + std::string("{\"type\":") + std::to_string(in.items[i].type) + ",\"name\":\"" + in.items[i].name + "\",\"fields\":[";
        for (size_t j = 0; j < in.items[i].fields.size(); j++) { if (j) o += ","; field(o, in.items[i].fields[j]); }
        o += "],\"dragPoints\":[";
        for (size_t j = 0; j < in.items[i].dragPoints.size(); j++) {
            o += j ? ",[" : "[";
            for (size_t k = 0; k < in.items[i].dragPoints[j].size(); k++) { if (k) o += ","; field(o, in.items[i].dragPoints[j][k]); }
            o += "]";
        }
        o += "],\"mesh\":{\"positions\":[";
        for (size_t j = 0; j < in.items[i].mesh.positions.size(); j++) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%s%.9g", j ? "," : "", in.items[i].mesh.positions[j]);
            o += buf;
        }
        o += "],\"indices\":[";
        for (size_t j = 0; j < in.items[i].mesh.indices.size(); j++) o += (j ? "," : "") + std::to_string(in.items[i].mesh.indices[j]);
        o += "]}}";
    }
    o += "],\"tableFields\":[";
    for (size_t i = 0; i < in.tableFields.size(); i++) { if (i) o += ","; field(o, in.tableFields[i]); }
    o += "],\"materials\":[";
    for (size_t i = 0; i < in.materials.size(); i++) {
        const auto &m = in.materials[i];
        char buf[256];
        std::snprintf(buf, sizeof buf, "{\"name\":\"%s\",\"elasticity\":%.9g,\"elasticityFalloff\":%.9g,\"friction\":%.9g,\"scatterAngle\":%.9g}",
                      m.name.c_str(), m.elasticity, m.elasticityFalloff, m.friction, m.scatterAngle);
        o += (i ? "," : "") + std::string(buf);
    }
    o += "]},\"expected\":{\"version\":\"vts1\",\"digest\":\"" + hex(s.top) + "\",\"sections\":{\"script\":\"" + hex(s.script) + "\",\"items\":\"" + hex(s.items) + "\",\"physics\":\"" + hex(s.physics) + "\"},\"procedures\":[";
    for (size_t i = 0; i < s.procedures.size(); i++) {
        o += (i ? "," : "") + std::string("[\"") + esc(s.procedures[i].key) + "\",\"" + hex(s.procedures[i].digest.data(), 8) + "\"]";
    }
    o += "]}}";
    o += last ? "\n" : ",\n";
}

int main()
{
    std::string o = "[\n";
    const auto unmodified = base();
    emit(o, "unmodified", "the reference table", unmodified, false);

    auto cosmetic = base();
    cosmetic.script = kCosmetic;
    cosmetic.helperScripts[0].content = "' ball saver\nsub enableballsaver()\n  ballsaver = true\nend sub\n";
    std::swap(cosmetic.items[0], cosmetic.items[2]);
    cosmetic.items[0].name = "ballsavetimer";
    cosmetic.items[2].fields[2].value = i32(0); // VSBL: visual only
    cosmetic.items[1].fields[5].value = str("WALL1"); // SURF is looked up ignoring case
    cosmetic.items[3].dragPoints[1][3].value = f32(0.5f); // TEXC: visual only
    cosmetic.tableFields[4].value = f32(40.f);  // OFFX: editor only
    std::swap(cosmetic.materials[0], cosmetic.materials[1]);
    emit(o, "cosmetic", "comments, case, whitespace, continuations, line endings, item and material order, a visual and an editor field: same signature as unmodified", cosmetic, false);

    auto scoring = base();
    scoring.script = std::string(kScript).replace(std::string(kScript).find("1000"), 4, "5000");
    emit(o, "scoring_edit", "a bumper awards 5000 instead of 1000: script section and the bumper1_hit procedure change", scoring, false);

    auto timer = base();
    timer.items[2].fields[1].value = i32(15000);
    emit(o, "timer_edit", "the ball-save timer interval doubles: items section changes", timer, false);

    auto flipper = base();
    flipper.items[0].fields[0].value = f32(3000.f);
    emit(o, "flipper_edit", "left flipper strength raised: items section changes", flipper, false);

    auto slope = base();
    slope.tableFields[1].value = f32(4.f);
    emit(o, "slope_edit", "table slope minimum flattened: physics section changes", slope, false);

    auto helper = base();
    helper.helperScripts[0].content = "Sub EnableBallSaver() : BallSaver = False : End Sub\r\n";
    emit(o, "helper_edit", "the table-local rules.vbs disables the ball saver: script section and the rules.vbs procedure change", helper, false);

    auto lineIf = base();
    const std::string from = " : Bonus = 1";
    lineIf.script = std::string(kScript).replace(std::string(kScript).find(from), from.size(), "\r\nBonus = 1");
    emit(o, "line_if_edit", "the bonus is split off the single-line If, so it is always awarded: script section changes", lineIf, false);

    auto moved = base();
    moved.items[1].fields[4].value = vec2(450.f, 640.f);
    emit(o, "bumper_moved", "the bumper moves 40 units down the playfield: items section changes", moved, false);

    auto outline = base();
    outline.items[3].dragPoints[1] = dp(520.f, 550.f);
    emit(o, "wall_reshaped", "one corner of a wall's outline moves: items section changes", outline, true);
    o += "]\n";
    std::fputs(o.c_str(), stdout);
}
