/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
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

#include <../source/achievements/report_outbox.h>
#include <../source/achievements/json_codec.h>
#include <../source/achievements/achievement_storage.h>
#include <catch2/catch_test_macros.hpp>
#include <boost/filesystem.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>

// clazy:excludeall=non-pod-global-static

using namespace scorbit::detail;
using namespace scorbit::detail::achievements;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

ReportItem item(std::string key, int64_t value, std::optional<bool> achieved = false)
{
    return ReportItem {std::move(key), achieved, {{0, RuleProgress {value, false}}}};
}

class TempDir
{
public:
    TempDir()
        : m_path {boost::filesystem::temp_directory_path()
                  / boost::filesystem::unique_path("sb-ach-%%%%-%%%%")}
    {
        boost::filesystem::create_directories(m_path);
    }
    ~TempDir() { boost::filesystem::remove_all(m_path); }
    const boost::filesystem::path &path() const { return m_path; }

private:
    boost::filesystem::path m_path;
};

} // namespace

TEST_CASE("Outbox waits for the session uuid", "[achievements]")
{
    ReportOutbox outbox;
    outbox.add("user-a", {item("k", 1)});
    CHECK_FALSE(outbox.next());

    outbox.setSessionUuid("9ab2");
    const auto request = outbox.next();
    REQUIRE(request);
    CHECK(request->sessionUuid == "9ab2");
    CHECK(request->userId == "user-a");
    CHECK(request->sequence == 1);
}

TEST_CASE("Outbox sends one request at a time, sequence shared by players", "[achievements]")
{
    ReportOutbox outbox;
    outbox.setSessionUuid("s");
    outbox.add("user-a", {item("k", 1)});
    outbox.add("user-b", {item("k", 2)});

    const auto first = outbox.next();
    REQUIRE(first);
    CHECK_FALSE(outbox.next()); // one in flight
    outbox.delivered();

    const auto second = outbox.next();
    REQUIRE(second);
    CHECK(second->userId != first->userId);
    CHECK(second->sequence == first->sequence + 1);
    outbox.delivered();
    CHECK(outbox.empty());
}

TEST_CASE("Outbox keeps only the latest item per player and key", "[achievements]")
{
    ReportOutbox outbox;
    outbox.setSessionUuid("s");
    outbox.add("user-a", {item("k", 1), item("j", 5)});
    outbox.add("user-a", {item("k", 3)});

    const auto request = outbox.next();
    REQUIRE(request);
    REQUIRE(request->items.size() == 2);
    for (const auto &i : request->items) {
        CHECK(i.rules.at(0).value == (i.key == "k" ? 3 : 5));
    }
}

TEST_CASE("Outbox retries a failed request with a fresh sequence and backoff", "[achievements]")
{
    ReportOutbox outbox;
    outbox.setSessionUuid("s");
    outbox.add("user-a", {item("k", 1)});

    const auto first = outbox.next();
    REQUIRE(first);

    // A newer value arrives while the request is in flight
    outbox.add("user-a", {item("k", 2)});

    CHECK(outbox.failed(true) == ReportOutbox::INITIAL_BACKOFF);
    CHECK_FALSE(outbox.next()); // backing off
    outbox.retryReady();

    const auto retry = outbox.next();
    REQUIRE(retry);
    CHECK(retry->sequence == first->sequence + 1);
    REQUIRE(retry->items.size() == 1);
    CHECK(retry->items[0].rules.at(0).value == 2); // the newer value won

    CHECK(outbox.failed(true) == 2 * ReportOutbox::INITIAL_BACKOFF);
    outbox.retryReady();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(outbox.next());
        CHECK(outbox.failed(true) <= ReportOutbox::MAX_BACKOFF);
        outbox.retryReady();
    }

    REQUIRE(outbox.next());
    outbox.delivered();
    outbox.add("user-a", {item("k", 3)});
    REQUIRE(outbox.next());
    CHECK(outbox.failed(true) == ReportOutbox::INITIAL_BACKOFF); // reset by the delivery
}

TEST_CASE("Outbox drops a request the server refused outright", "[achievements]")
{
    ReportOutbox outbox;
    outbox.setSessionUuid("s");
    outbox.add("user-a", {item("k", 1)});
    REQUIRE(outbox.next());
    CHECK(outbox.failed(false) == 0ms);
    CHECK(outbox.empty());
}

TEST_CASE("Report request encoding", "[achievements]")
{
    ReportRequest request {"3f1c", "9ab2", 4, {}};
    request.items.push_back(
            ReportItem {"game-cv-boom-balloon", true, {{0, RuleProgress {1, true}}}});
    request.items.push_back(
            ReportItem {"game-cv-chain-finale", std::nullopt, {{0, RuleProgress {3, true}}}});

    const auto body = json::parse(encodeReportRequest(request));
    CHECK(body["user_id"] == "3f1c");
    CHECK(body["session_uuid"] == "9ab2");
    CHECK(body["sequence"] == 4);
    REQUIRE(body["achievements"].size() == 2);

    const auto &boom = body["achievements"][0];
    CHECK(boom["key"] == "game-cv-boom-balloon");
    CHECK(boom["achieved"] == true);
    CHECK(boom["rule_progress"] == json::parse(R"([{"index":0,"value":1,"satisfied":true}])"));

    const auto &finale = body["achievements"][1];
    CHECK_FALSE(finale.contains("achieved")); // no verdict: omitted, never null
    CHECK(finale["rule_progress"].size() == 1);
}

TEST_CASE("Report response parsing", "[achievements]")
{
    const auto outcomes = parseReportResponse(json::parse(R"({"results":[
        {"key":"game-cv-boom-balloon","status":"unlocked","user_achievement":{}},
        {"key":"game-cv-spin-master","status":"in_progress"},
        {"key":"game-cv-marathon","status":"already_held"},
        {"key":"game-cv-retired","status":"rejected","code":"not_published","detail":"Nope."},
        {"key":"game-cv-future","status":"granted_by_magic"}
    ]})"));

    REQUIRE(outcomes);
    REQUIRE(outcomes->size() == 4);
    CHECK((*outcomes)[0].status == ReportStatus::Unlocked);
    CHECK((*outcomes)[1].status == ReportStatus::InProgress);
    CHECK((*outcomes)[2].status == ReportStatus::AlreadyHeld);
    CHECK((*outcomes)[3].status == ReportStatus::Rejected);
    CHECK((*outcomes)[3].code == "not_published");
    CHECK_FALSE(parseReportResponse(json::parse(R"({"detail":"x"})")));
}

TEST_CASE("Definitions persist", "[achievements]")
{
    TempDir dir;
    AchievementStorage storage {dir.path().string()};
    CHECK_FALSE(storage.loadDefinitions());

    REQUIRE(storage.saveDefinitions(R"({"results":[]})"));
    CHECK(AchievementStorage {dir.path().string()}.loadDefinitions() == R"({"results":[]})");
}

TEST_CASE("Frames install per key and track their version", "[achievements]")
{
    TempDir dir;
    AchievementStorage storage {dir.path().string()};
    CHECK(storage.frameVersions().empty());

    const auto install = [&](const std::string &key, const std::string &content,
                             const std::string &extension, int64_t version) {
        const auto path = storage.frameDownloadPath(key);
        std::ofstream(path, std::ios::binary) << content;
        return storage.installFrame(key, path, extension, version);
    };

    REQUIRE(install("game-cv-boom", "PNG1", ".png", 3));
    REQUIRE(install("game-cv-blast", "BIN", ".bin", 1));
    CHECK(storage.frameVersions()
          == std::map<std::string, int64_t> {{"game-cv-blast", 1}, {"game-cv-boom", 3}});

    const auto boom = storage.frame("game-cv-boom");
    REQUIRE(boom);
    CHECK(std::string(boom->begin(), boom->end()) == "PNG1");
    CHECK_FALSE(storage.frame("game-cv-missing"));
    CHECK_FALSE(storage.frame("../definitions"));

    SECTION("A new version replaces the frame, whatever its extension")
    {
        REQUIRE(install("game-cv-boom", "GIF2", ".gif", 4));
        const auto again = AchievementStorage {dir.path().string()};
        CHECK(again.frameVersions().at("game-cv-boom") == 4);
        const auto frame = again.frame("game-cv-boom");
        REQUIRE(frame);
        CHECK(std::string(frame->begin(), frame->end()) == "GIF2");
        CHECK_FALSE(boost::filesystem::exists(dir.path() / "achievements/frames/game-cv-boom.png"));
    }

    SECTION("A removed frame is gone with its version")
    {
        REQUIRE(storage.removeFrame("game-cv-boom"));
        CHECK_FALSE(storage.frame("game-cv-boom"));
        CHECK(storage.frameVersions() == std::map<std::string, int64_t> {{"game-cv-blast", 1}});
        CHECK(storage.removeFrame("game-cv-never"));
    }

    SECTION("A key that can't name a file is refused")
    {
        const auto path = storage.frameDownloadPath("x");
        std::ofstream(path, std::ios::binary) << "X";
        CHECK_FALSE(storage.installFrame("../escape", path, ".png", 1));
    }
}

TEST_CASE("An older SDK's cache is cleaned up", "[achievements]")
{
    TempDir dir;
    const auto root = dir.path() / "achievements";
    boost::filesystem::create_directories(root / "frames/128x32");
    std::ofstream((root / "definitions.etag").string()) << "\"v1\"";
    std::ofstream((root / "frames.version").string()) << "7";
    std::ofstream((root / "frames.zip").string()) << "zip";
    std::ofstream((root / "frames/128x32/game-cv-boom.png").string()) << "old";
    std::ofstream((root / "definitions.json").string()) << "[]";

    AchievementStorage storage {dir.path().string()};
    CHECK_FALSE(boost::filesystem::exists(root / "definitions.etag"));
    CHECK_FALSE(boost::filesystem::exists(root / "frames.version"));
    CHECK_FALSE(boost::filesystem::exists(root / "frames.zip"));
    CHECK_FALSE(boost::filesystem::exists(root / "frames"));
    CHECK_FALSE(storage.frame("game-cv-boom"));
    CHECK(storage.loadDefinitions() == "[]");
}

TEST_CASE("Frame extension comes from the URL path", "[achievements]")
{
    CHECK(frameExtension("https://cdn.example/achievement_frame/game-cv-boom_3.png") == ".png");
    CHECK(frameExtension("https://s3.example/a/game-cv-boom_3.PNG?X-Amz-Signature=ab.cd")
          == ".png");
    CHECK(frameExtension("https://cdn.example/a/game-cv-boom_3.gif#x") == ".gif");
    CHECK(frameExtension("https://cdn.example/a/game-cv-boom") == ".png");
    CHECK(frameExtension("https://cdn.example/a.b/frame") == ".png");
    CHECK(frameExtension("https://cdn.example/a/x.p/ng") == ".png");
    CHECK(frameExtension("/media/a/x.toolongextension") == ".png");
}
