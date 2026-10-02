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

#include <scorbit_sdk/scorbit_sdk.h>
#include <scorbit_sdk/scorbit_sdk_c.h>

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#ifdef _WIN32
#    include <direct.h>
#else
#    include <sys/stat.h>
#    include <unistd.h>
#endif

// clazy:excludeall=non-pod-global-static

namespace {

int dummySigner(uint8_t signature[SB_SIGNATURE_MAX_LENGTH], size_t *signature_len,
                const uint8_t digest[SB_DIGEST_LENGTH], void *user_data)
{
    (void)signature;
    (void)signature_len;
    (void)digest;
    (void)user_data;
    return 0;
}

// The interface tests are C++14: no std::filesystem
void makeDir(const std::string &path)
{
#ifdef _WIN32
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

void removeDir(const std::string &path)
{
#ifdef _WIN32
    _rmdir(path.c_str());
#else
    rmdir(path.c_str());
#endif
}

std::string tempDir()
{
    for (const char *name : {"TMPDIR", "TEMP", "TMP"}) {
        if (const char *dir = std::getenv(name)) {
            return dir;
        }
    }
    return "/tmp";
}

} // namespace

TEST_CASE("Achievements C API on a fresh game state", "[Achievements]")
{
    // Nothing is written until definitions are fetched, which needs a real backend
    const std::string dataDir = tempDir() + "/sb-achievements-test";

    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, "vscorbitron");
    sb_config_set_machine_id(cfg, 4419);
    sb_config_set_game_code_version(cfg, "0.1.0");
    sb_config_set_signer(cfg, dummySigner, nullptr);
    sb_config_set_data_dir(cfg, dataDir.c_str());

    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    // Nothing is cached yet: every lookup fails cleanly
    CHECK(sb_achievements_count(h) == 0);
    sb_achievement_t achievement;
    CHECK_FALSE(sb_achievement_at(h, 0, &achievement));
    CHECK_FALSE(sb_achievement_find(h, "game-cv-boom", &achievement));
    CHECK_FALSE(sb_achievement_find(h, nullptr, &achievement));
    sb_achievement_rule_t rule;
    CHECK_FALSE(sb_achievement_rule_at(h, "game-cv-boom", 0, &rule));
    sb_achievement_progress_t progress;
    CHECK_FALSE(sb_achievement_player_progress(h, 1, "game-cv-boom", &progress));
    sb_achievement_rule_progress_t ruleProgress;
    CHECK_FALSE(sb_achievement_player_rule_progress(h, 1, "game-cv-boom", 0, &ruleProgress));
    const uint8_t *frame = nullptr;
    size_t frameSize = 0;
    CHECK_FALSE(sb_achievement_frame(h, "game-cv-boom", &frame, &frameSize));

    // Game code drives events like any other game state; evaluation needs no call
    sb_set_game_started(h, SB_GAME_STARTED_BY_BUTTON);
    sb_add_event(h, "spins", 3);
    sb_add_event(h, "bad;name", 1);
    sb_commit(h);
    sb_flush_achievement_reports(h);
    sb_refresh_achievements(h);
    sb_download_achievement_frames(h);
    sb_set_game_finished(h);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

TEST_CASE("Cached definitions are served through the C and C++ API", "[Achievements]")
{
    // The API's `GET achievements/scorbitron/` body, as an earlier boot cached it
    const std::string dataDir = tempDir() + "/sb-achievements-cached-test";
    const std::string cacheDir = dataDir + "/achievements";
    const std::string definitionsFile = cacheDir + "/definitions.json";
    makeDir(dataDir);
    makeDir(cacheDir);
    std::ofstream(definitionsFile) << R"([
        {"id": "0b7c4f0e-5c1e-4f55-9a43-0f6d2b8f6a11", "key": "game-cv-boom",
         "name": "Boom", "description": "Pop it.", "icon": "https://cdn.example/boom.png",
         "is_single_session": false, "is_trophy": false, "is_badge": true, "scope": "game",
         "visible": true, "obscure": false, "obscure_image": null,
         "group_id": 7, "level": 2000, "display_position": 2,
         "frame": null, "frame_version": 0, "notify_when_achieved": true,
         "rules": [{"type": "MODE", "comparison": ">=", "target": 3, "reference": "balloon",
                    "subachievement": null}]}
    ])";

    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, "vscorbitron");
    sb_config_set_machine_id(cfg, 4419);
    sb_config_set_game_code_version(cfg, "0.1.0");
    sb_config_set_signer(cfg, dummySigner, nullptr);
    sb_config_set_data_dir(cfg, dataDir.c_str());

    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    REQUIRE(sb_achievements_count(h) == 1);
    sb_achievement_t c;
    REQUIRE(sb_achievement_find(h, "game-cv-boom", &c));
    CHECK(std::string(c.icon_url) == "https://cdn.example/boom.png");
    CHECK(c.evaluation == SB_ACHIEVEMENT_UNLIMITED);
    CHECK(c.is_badge);
    CHECK(c.has_group);
    CHECK(c.group_id == 7);
    CHECK(c.level == 2000);
    CHECK(c.display_position == 2);

    sb_achievement_rule_t rule;
    REQUIRE(sb_achievement_rule_at(h, "game-cv-boom", 0, &rule));
    CHECK(rule.comparison == SB_ACHIEVEMENT_GE);
    CHECK(rule.target == 3);

    const auto a = scorbit::achievements_detail::fromC(h, c);
    CHECK(a.isBadge);
    CHECK(a.level == 2000);
    REQUIRE(a.rules.size() == 1);
    CHECK(a.rules[0].comparison == scorbit::AchievementComparison::Ge);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
    std::remove(definitionsFile.c_str());
    removeDir(cacheDir);
    removeDir(dataDir);
}

TEST_CASE("Achievement event helpers reject other events", "[Achievements]")
{
    const char *key = nullptr;
    CHECK_FALSE(sb_event_achievement_updated(nullptr, &key, nullptr, nullptr, nullptr));
    CHECK(sb_event_achievement_rules_count(nullptr) == 0);
    CHECK_FALSE(sb_event_achievement_rule(nullptr, 0, nullptr, nullptr, nullptr));

    scorbit::AchievementUpdate update;
    CHECK_FALSE(scorbit::Event {nullptr}.getAchievementUpdated(update));
}
