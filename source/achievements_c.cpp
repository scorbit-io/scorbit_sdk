/*
 * Scorbit SDK
 *
 * (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
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

#include <scorbit_sdk/achievements_c.h>
#include <scorbit_sdk/achievements.h>
#include "game_state_impl.h"
#include <vector>
#include <string>
#include <memory>

using namespace scorbit;
using namespace detail;

// Forward declaration of the game state struct (defined in game_state_c.cpp)
struct sb_game_state_struct {
    detail::GameStateImpl gameState;
};

namespace {

// Helper class to manage string lifetimes for C callbacks
class AchievementDataHolder
{
public:
    std::vector<Achievement> achievements;
    std::vector<sb_achievement_t> cAchievements;

    void convertToCFormat()
    {
        cAchievements.clear();
        cAchievements.reserve(achievements.size());

        for (const auto &ach : achievements) {
            sb_achievement_t cAch;
            cAch.key = ach.key.c_str();
            cAch.name = ach.name.c_str();
            cAch.description = ach.description.c_str();
            cAch.count = ach.count;
            cAch.image_url = ach.imageUrl.c_str();
            cAch.obscure_image_url = ach.obscureImageUrl.c_str();
            cAch.obscure = ach.obscure;
            cAch.visible = ach.visible;
            cAch.is_trophy = ach.isTrophy;
            cAch.notify_when_achieved = ach.notifyWhenAchieved;
            cAch.input_time = static_cast<sb_achievement_input_time_t>(ach.inputTime);
            cAch.trigger = static_cast<sb_achievement_trigger_t>(ach.trigger);
            cAch.mode_type = static_cast<sb_achievement_mode_type_t>(ach.modeType);
            cAch.mode_name = ach.modeName.c_str();
            cAch.target_score = ach.targetScore;
            cAch.group_id = ach.groupId;
            cAch.achievement_id = ach.achievementId;
            cAch.rules_count = ach.rules.size();
            cAchievements.push_back(cAch);
        }
    }
};

class ProgressDataHolder
{
public:
    std::vector<AchievementProgress> progress;
    std::vector<sb_achievement_progress_t> cProgress;

    void convertToCFormat()
    {
        cProgress.clear();
        cProgress.reserve(progress.size());

        for (const auto &prog : progress) {
            sb_achievement_progress_t cProg;
            cProg.key = prog.key.c_str();
            cProg.progress = prog.progress;
            cProg.unlocked = prog.unlocked;
            cProg.unlocked_at = prog.unlockedAt.empty() ? nullptr : prog.unlockedAt.c_str();
            cProgress.push_back(cProg);
        }
    }
};

class UnlockResultHolder
{
public:
    AchievementUnlockResult result;
    sb_achievement_unlock_result_t cResult;

    void convertToCFormat()
    {
        cResult.key = result.key.c_str();
        cResult.success = result.success;
        cResult.newly_unlocked = result.newlyUnlocked;
        cResult.message = result.message.empty() ? nullptr : result.message.c_str();
    }
};

} // namespace

void sb_fetch_achievements(sb_game_handle_t handle, sb_achievements_callback_t callback,
                           void *user_data)
{
    if (!handle || !callback) {
        if (callback) {
            callback(SB_EC_API_ERROR, nullptr, 0, user_data);
        }
        return;
    }

    // Create a shared holder that survives until callback completes
    auto holder = std::make_shared<AchievementDataHolder>();

    handle->gameState.fetchAchievements(
            [callback, user_data, holder](Error error, std::vector<Achievement> achievements) {
                holder->achievements = std::move(achievements);
                holder->convertToCFormat();

                callback(static_cast<sb_error_t>(error),
                         holder->cAchievements.empty() ? nullptr : holder->cAchievements.data(),
                         holder->cAchievements.size(), user_data);
            });
}

void sb_fetch_achievement_progress(sb_game_handle_t handle, int64_t user_id,
                                   sb_achievement_progress_callback_t callback, void *user_data)
{
    if (!handle || !callback) {
        if (callback) {
            callback(SB_EC_API_ERROR, nullptr, 0, user_data);
        }
        return;
    }

    auto holder = std::make_shared<ProgressDataHolder>();

    handle->gameState.fetchAchievementProgress(
            user_id,
            [callback, user_data, holder](Error error, std::vector<AchievementProgress> progress) {
                holder->progress = std::move(progress);
                holder->convertToCFormat();

                callback(static_cast<sb_error_t>(error),
                         holder->cProgress.empty() ? nullptr : holder->cProgress.data(),
                         holder->cProgress.size(), user_data);
            });
}

void sb_unlock_achievement(sb_game_handle_t handle, int64_t user_id, const char *achievement_key,
                           int count, sb_achievement_unlock_callback_t callback, void *user_data)
{
    if (!handle || !achievement_key || !callback) {
        if (callback) {
            sb_achievement_unlock_result_t result = {};
            result.key = achievement_key ? achievement_key : "";
            result.success = false;
            result.newly_unlocked = false;
            result.message = "Invalid parameters";
            callback(SB_EC_API_ERROR, &result, user_data);
        }
        return;
    }

    auto holder = std::make_shared<UnlockResultHolder>();
    std::string key(achievement_key);

    handle->gameState.unlockAchievement(
            user_id, key, count,
            [callback, user_data, holder](Error error, AchievementUnlockResult result) {
                holder->result = std::move(result);
                holder->convertToCFormat();
                callback(static_cast<sb_error_t>(error), &holder->cResult, user_data);
            });
}

void sb_lock_achievement(sb_game_handle_t handle, int64_t user_id, const char *achievement_key,
                         sb_achievement_unlock_callback_t callback, void *user_data)
{
    if (!handle || !achievement_key || !callback) {
        if (callback) {
            sb_achievement_unlock_result_t result = {};
            result.key = achievement_key ? achievement_key : "";
            result.success = false;
            result.newly_unlocked = false;
            result.message = "Invalid parameters";
            callback(SB_EC_API_ERROR, &result, user_data);
        }
        return;
    }

    auto holder = std::make_shared<UnlockResultHolder>();
    std::string key(achievement_key);

    handle->gameState.lockAchievement(
            user_id, key,
            [callback, user_data, holder](Error error, AchievementUnlockResult result) {
                holder->result = std::move(result);
                holder->convertToCFormat();
                callback(static_cast<sb_error_t>(error), &holder->cResult, user_data);
            });
}

// ------------------------------------------------------------------------------------------------
// Achievement Cache and Local Matching
// ------------------------------------------------------------------------------------------------

namespace {

// Thread-local storage for cached achievement data returned by C API functions
// This ensures string pointers remain valid until the next call
thread_local AchievementDataHolder g_cachedAchievements;
thread_local ProgressDataHolder g_cachedProgress;
thread_local std::vector<std::string> g_matchedKeys;

// Callback wrapper storage
struct TriggeredCallbackData {
    sb_achievement_triggered_callback_t callback = nullptr;
    void *userData = nullptr;
};

} // namespace

bool sb_has_achievements(sb_game_handle_t handle)
{
    if (!handle) {
        return false;
    }
    return handle->gameState.hasAchievements();
}

size_t sb_get_cached_achievements_count(sb_game_handle_t handle)
{
    if (!handle) {
        return 0;
    }
    auto achievements = handle->gameState.getAchievements();
    return achievements.size();
}

bool sb_get_cached_achievement_at(sb_game_handle_t handle, size_t index,
                                  sb_achievement_t *achievement)
{
    if (!handle || !achievement) {
        return false;
    }

    g_cachedAchievements.achievements = handle->gameState.getAchievements();
    if (index >= g_cachedAchievements.achievements.size()) {
        return false;
    }

    g_cachedAchievements.convertToCFormat();
    *achievement = g_cachedAchievements.cAchievements[index];
    return true;
}

bool sb_get_cached_achievement(sb_game_handle_t handle, const char *key,
                               sb_achievement_t *achievement)
{
    if (!handle || !key || !achievement) {
        return false;
    }

    auto achOpt = handle->gameState.getAchievement(std::string(key));
    if (!achOpt) {
        return false;
    }

    g_cachedAchievements.achievements.clear();
    g_cachedAchievements.achievements.push_back(*achOpt);
    g_cachedAchievements.convertToCFormat();
    *achievement = g_cachedAchievements.cAchievements[0];
    return true;
}

bool sb_get_cached_progress(sb_game_handle_t handle, int64_t user_id, const char *key,
                            sb_achievement_progress_t *progress)
{
    if (!handle || !key || !progress) {
        return false;
    }

    auto progOpt = handle->gameState.getProgress(user_id, std::string(key));
    if (!progOpt) {
        return false;
    }

    g_cachedProgress.progress.clear();
    g_cachedProgress.progress.push_back(*progOpt);
    g_cachedProgress.convertToCFormat();
    *progress = g_cachedProgress.cProgress[0];
    return true;
}

size_t sb_check_mode_achievements(sb_game_handle_t handle, const char *mode_name,
                                  const char *mode_type, int64_t user_id, const char **keys,
                                  size_t max_keys)
{
    if (!handle || !mode_name || !mode_type) {
        return 0;
    }

    g_matchedKeys = handle->gameState.checkModeAchievements(std::string(mode_name),
                                                            std::string(mode_type), user_id);

    size_t count = std::min(g_matchedKeys.size(), max_keys);
    if (keys) {
        for (size_t i = 0; i < count; ++i) {
            keys[i] = g_matchedKeys[i].c_str();
        }
    }
    return g_matchedKeys.size();
}

size_t sb_check_score_achievements(sb_game_handle_t handle, int64_t score, int64_t user_id,
                                   const char **keys, size_t max_keys)
{
    if (!handle) {
        return 0;
    }

    g_matchedKeys = handle->gameState.checkScoreAchievements(score, user_id);

    size_t count = std::min(g_matchedKeys.size(), max_keys);
    if (keys) {
        for (size_t i = 0; i < count; ++i) {
            keys[i] = g_matchedKeys[i].c_str();
        }
    }
    return g_matchedKeys.size();
}

bool sb_increment_achievement_progress(sb_game_handle_t handle, const char *key, int64_t user_id,
                                       int increment)
{
    if (!handle || !key) {
        return false;
    }
    return handle->gameState.incrementProgress(std::string(key), user_id, increment);
}

void sb_set_achievement_triggered_callback(sb_game_handle_t handle,
                                           sb_achievement_triggered_callback_t callback,
                                           void *user_data)
{
    if (!handle) {
        return;
    }

    if (callback) {
        handle->gameState.setAchievementTriggeredCallback(
                [callback, user_data](const std::string &key, int64_t userId, bool isUnlock,
                                      int progress) {
                    callback(key.c_str(), userId, isUnlock, progress, user_data);
                });
    } else {
        handle->gameState.setAchievementTriggeredCallback(nullptr);
    }
}

// ------------------------------------------------------------------------------------------------
// Achievement Rule Accessors (v2)
// ------------------------------------------------------------------------------------------------

namespace {
// Thread-local storage for rule data returned by C API
thread_local std::vector<AchievementRule> g_cachedRules;
thread_local std::vector<sb_achievement_rule_t> g_cRules;
} // namespace

size_t sb_achievement_get_rules_count(sb_game_handle_t handle, const char *achievement_key)
{
    if (!handle || !achievement_key) {
        return 0;
    }

    auto achOpt = handle->gameState.getAchievement(std::string(achievement_key));
    if (!achOpt) {
        return 0;
    }

    return achOpt->rules.size();
}

bool sb_achievement_get_rule_at(sb_game_handle_t handle, const char *achievement_key, size_t index,
                                sb_achievement_rule_t *rule)
{
    if (!handle || !achievement_key || !rule) {
        return false;
    }

    auto achOpt = handle->gameState.getAchievement(std::string(achievement_key));
    if (!achOpt || index >= achOpt->rules.size()) {
        return false;
    }

    // Cache the rules to keep string pointers valid
    g_cachedRules = achOpt->rules;
    const auto &cachedRule = g_cachedRules[index];

    rule->type = cachedRule.type.c_str();
    rule->comparison = cachedRule.comparison.c_str();
    rule->target = cachedRule.target;
    rule->reference = cachedRule.reference.c_str();
    rule->subachievement_id = cachedRule.subachievementId;

    return true;
}

// ------------------------------------------------------------------------------------------------
// DMD Frame Cache (Internal for scorbitd)
// ------------------------------------------------------------------------------------------------

namespace {
// Thread-local storage for DMD frame data returned by C API
thread_local std::vector<uint8_t> g_dmdFrameBuffer;
} // namespace

void sb_download_achievement_frames(sb_game_handle_t handle)
{
    if (!handle) {
        return;
    }
    handle->gameState.downloadAchievementFrames();
}

bool sb_has_dmd_frame(sb_game_handle_t handle, const char *key)
{
    if (!handle || !key) {
        return false;
    }
    return handle->gameState.hasDmdFrame(std::string(key));
}

const uint8_t *sb_get_dmd_frame(sb_game_handle_t handle, const char *key, size_t *size)
{
    if (!handle || !key || !size) {
        if (size) {
            *size = 0;
        }
        return nullptr;
    }

    g_dmdFrameBuffer = handle->gameState.getDmdFrame(std::string(key));
    *size = g_dmdFrameBuffer.size();
    return g_dmdFrameBuffer.empty() ? nullptr : g_dmdFrameBuffer.data();
}
