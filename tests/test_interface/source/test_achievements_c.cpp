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
#include <cstdlib>
#include <string>

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

} // namespace

TEST_CASE("Achievements C API on a fresh game state", "[Achievements]")
{
    // Nothing is written until definitions are fetched, which needs a real backend
    const char *tmp = std::getenv("TMPDIR");
    const std::string dataDir = std::string(tmp ? tmp : "/tmp") + "/sb-achievements-test";

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

TEST_CASE("Achievement event helpers reject other events", "[Achievements]")
{
    const char *key = nullptr;
    CHECK_FALSE(sb_event_achievement_updated(nullptr, &key, nullptr, nullptr, nullptr));
    CHECK(sb_event_achievement_rules_count(nullptr) == 0);
    CHECK_FALSE(sb_event_achievement_rule(nullptr, 0, nullptr, nullptr, nullptr));

    scorbit::AchievementUpdate update;
    CHECK_FALSE(scorbit::Event {nullptr}.getAchievementUpdated(update));
}
