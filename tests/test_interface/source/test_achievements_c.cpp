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
    sb_config_set_achievement_artwork(cfg, SB_ACHIEVEMENT_ARTWORK_FRAME);

    sb_game_handle_t h = sb_create_game_state(cfg);
    REQUIRE(h != nullptr);

    // Game code drives game events like any other game state; evaluation needs no call
    sb_set_game_started(h, SB_GAME_STARTED_BY_BUTTON);
    sb_add_game_event(h, "spins", 3);
    sb_add_game_event(h, "spins", -1);
    sb_add_game_event(h, "bad;name", 1);
    sb_add_game_event(h, nullptr, 1);
    sb_commit(h);
    sb_set_game_finished(h);

    sb_destroy_game_state(h);
    sb_config_destroy(cfg);
}

TEST_CASE("Config chooses the achievement artwork", "[Achievements][C++]")
{
    sb_config_set_achievement_artwork(nullptr, SB_ACHIEVEMENT_ARTWORK_IMAGE); // ignored

    scorbit::Config config;
    config.setDataDir(tempDir()).setAchievementArtwork(scorbit::AchievementArtwork::Image);
    CHECK(config.isValid());
    CHECK(static_cast<int>(scorbit::AchievementArtwork::Frame) == SB_ACHIEVEMENT_ARTWORK_FRAME);
}

TEST_CASE("Achievement event helpers reject other events", "[Achievements]")
{
    const char *str = nullptr;
    sb_player_t player = 0;
    const uint8_t *data = nullptr;
    size_t size = 0;
    CHECK_FALSE(sb_event_achievement_unlocked(nullptr, &str, &player));
    CHECK_FALSE(sb_event_achievement_name(nullptr, &str));
    CHECK_FALSE(sb_event_achievement_description(nullptr, &str));
    CHECK_FALSE(sb_event_achievement_artwork(nullptr, &data, &size));

    scorbit::AchievementUnlock unlock;
    CHECK_FALSE(scorbit::Event {nullptr}.getAchievementUnlocked(unlock));
    CHECK(static_cast<int>(scorbit::EventType::AchievementUnlocked) == SB_EVT_ACHIEVEMENT_UNLOCKED);
}
