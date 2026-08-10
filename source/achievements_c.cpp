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

/**
 * @file achievements_c.cpp
 * @brief C shim for the achievements API declared in achievements_c.h.
 *
 * The four network calls (`sb_fetch_achievements`, `sb_fetch_achievement_progress`,
 * `sb_unlock_achievement`, `sb_lock_achievement`) are posted through the C API job queue, like
 * every other network call. Everything else - the cache accessors, the local matchers,
 * `sb_increment_achievement_progress` - calls `handle->gameState` directly: those functions return
 * their result synchronously per the signatures in achievements_c.h, and
 * @ref scorbit::detail::AchievementManager is internally thread-safe, so there is nothing to
 * serialise. See @ref gsh_convention in game_state_handle.h.
 *
 * @section ach_c_lifetime String lifetimes
 *
 * achievements_c.h promises that strings and matched-key pointers stay valid "until the next
 * achievement API call". The manager hands back copies, so this file parks those copies in a
 * thread_local scratch area whose slots are overwritten by the next call of the same kind. Per
 * thread rather than per handle, so two threads reading the cache concurrently cannot invalidate
 * each other's pointers.
 */

#include <scorbit_sdk/achievements_c.h>

#include "achievement_manager.h"
#include "game_state_handle.h"
#include "identifiers.h" // TEMPORARY - only needed for sb_debug_seed_achievements below

#include <nlohmann/json.hpp> // TEMPORARY - only needed for sb_debug_seed_achievements below

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

using namespace scorbit;
using namespace scorbit::detail;
using namespace scorbit_c_api_queue;

namespace {

/// Backing store for the `const char *` fields the synchronous accessors hand out.
struct AchievementScratch {
    Achievement achievement;                 ///< backs sb_get_cached_achievement{,_at}
    AchievementRule rule;                    ///< backs sb_achievement_get_rule_at
    AchievementProgress progress;            ///< backs sb_get_cached_progress
    std::vector<std::string> matchedKeys;    ///< backs the check*() key out-arrays
    DmdFrame frame;                          ///< backs sb_get_dmd_frame
};

thread_local AchievementScratch g_scratch;

/// Fill a `sb_achievement_t` from @p src. @p src must outlive every use of @p out.
void toCAchievement(const Achievement &src, sb_achievement_t *out)
{
    out->key = src.key.c_str();
    out->name = src.name.c_str();
    out->description = src.description.c_str();
    out->scope = src.scope.c_str();
    out->image_url = src.imageUrl.c_str();
    out->obscure_image_url = src.obscureImageUrl.c_str();
    out->obscure = src.obscure;
    out->visible = src.visible;
    out->is_trophy = src.isTrophy;
    out->notify_when_achieved = src.notifyWhenAchieved;
    out->input_time = static_cast<sb_achievement_input_time_t>(src.inputTime);
    out->trigger = static_cast<sb_achievement_trigger_t>(src.trigger);
    out->mode_type = static_cast<sb_achievement_mode_type_t>(src.modeType);
    out->mode_name = src.modeName.c_str();
    out->target_score = src.targetScore;
    out->group_id = src.groupId;
    out->level = src.level;
    out->ball_count = src.ballCount;
    out->rules_count = src.rules.size();
}

/// Fill a `sb_achievement_rule_t` from @p src. @p src must outlive every use of @p out.
void toCRule(const AchievementRule &src, sb_achievement_rule_t *out)
{
    out->type = src.type.c_str();
    out->comparison = src.comparison.c_str();
    out->target = src.target;
    out->reference = src.reference.c_str();
    out->subachievement_id = src.subachievementId;
}

/// Fill a `sb_achievement_progress_t` from @p src. @p src must outlive every use of @p out.
void toCProgress(const AchievementProgress &src, sb_achievement_progress_t *out)
{
    out->key = src.key.c_str();
    out->progress = src.progress;
    out->unlocked = src.unlocked;
    // achievements_c.h documents unlocked_at as NULL when the achievement is not unlocked.
    out->unlocked_at = src.unlockedAt.empty() ? nullptr : src.unlockedAt.c_str();
}

/// Copy @p keys into the scratch area and publish stable pointers into @p out.
size_t publishMatchedKeys(std::vector<std::string> keys, const char **out, size_t maxKeys)
{
    g_scratch.matchedKeys = std::move(keys);

    if (!out || maxKeys == 0) {
        return 0;
    }

    const auto count = std::min(g_scratch.matchedKeys.size(), maxKeys);
    for (size_t i = 0; i < count; ++i) {
        out[i] = g_scratch.matchedKeys[i].c_str();
    }
    return count;
}

} // namespace

namespace scorbit_c_api_queue {

AchievementsCallback makeAchievementsReplyBridge(sb_achievements_callback_t cb, void *user_data)
{
    return [cb, user_data](Error error, std::vector<Achievement> achievements) {
        if (!cb) {
            return;
        }
        if (error != Error::Success) {
            cb(static_cast<sb_error_t>(error), nullptr, 0, user_data);
            return;
        }

        // achievements stays alive for the whole callback, so the pointers into it are valid for
        // exactly as long as achievements_c.h promises.
        std::vector<sb_achievement_t> cAchievements(achievements.size());
        for (size_t i = 0; i < achievements.size(); ++i) {
            toCAchievement(achievements[i], &cAchievements[i]);
        }
        cb(static_cast<sb_error_t>(error), cAchievements.data(), cAchievements.size(), user_data);
    };
}

AchievementProgressCallback
makeAchievementProgressReplyBridge(sb_achievement_progress_callback_t cb, void *user_data)
{
    return [cb, user_data](Error error, std::vector<AchievementProgress> progress) {
        if (!cb) {
            return;
        }
        if (error != Error::Success) {
            cb(static_cast<sb_error_t>(error), nullptr, 0, user_data);
            return;
        }

        std::vector<sb_achievement_progress_t> cProgress(progress.size());
        for (size_t i = 0; i < progress.size(); ++i) {
            toCProgress(progress[i], &cProgress[i]);
        }
        cb(static_cast<sb_error_t>(error), cProgress.data(), cProgress.size(), user_data);
    };
}

AchievementUnlockCallback makeAchievementUnlockReplyBridge(sb_achievement_unlock_callback_t cb,
                                                           void *user_data)
{
    return [cb, user_data](Error error, AchievementUnlockResult result) {
        if (!cb) {
            return;
        }
        if (error != Error::Success) {
            cb(static_cast<sb_error_t>(error), nullptr, user_data);
            return;
        }

        sb_achievement_unlock_result_t cResult {};
        cResult.key = result.key.c_str();
        cResult.success = result.success;
        cResult.newly_unlocked = result.newlyUnlocked;
        // The server response carries no message field, so this is normally NULL.
        cResult.message = result.message.empty() ? nullptr : result.message.c_str();
        cb(static_cast<sb_error_t>(error), &cResult, user_data);
    };
}

} // namespace scorbit_c_api_queue

// ------------------------------------------------------------------------------------------------
// Network - queued, like every other network call in the C API
// ------------------------------------------------------------------------------------------------

void sb_fetch_achievements(sb_game_handle_t handle, sb_achievements_callback_t callback,
                           void *user_data)
{
    if (!handle) {
        return;
    }
    handle->postApiJob(JobFetchAchievements {handle, callback, user_data});
}

// TEMPORARY - LOCAL TESTING ONLY - DELETE BEFORE COMMIT. Mirrors the field-parsing subset of
// Net::fetchAchievements (net.cpp) that matters for local matching - key/scope/inputTime/rules -
// without touching net.cpp itself. Synchronous and direct, like the other cache mutators; no job
// queue since there is no network call to serialize against.
bool sb_debug_seed_achievements(sb_game_handle_t handle, const char *json)
{
    if (!handle || !json) {
        return false;
    }

    using namespace scorbit::detail;

    std::vector<Achievement> achievements;
    try {
        const auto j = nlohmann::json::parse(json);
        if (!j.is_array()) {
            return false;
        }

        for (const auto &item : j) {
            if (!item.is_object()) {
                continue;
            }
            Achievement ach;
            ach.key = item.value(JKEY_ACH_KEY, std::string {});
            ach.name = item.value(JKEY_ACH_NAME, std::string {});
            ach.description = item.value(JKEY_ACH_DESCRIPTION, std::string {});
            ach.scope = item.value(JKEY_ACH_SCOPE, std::string {});
            ach.imageUrl = item.value(JKEY_ACH_ICON, std::string {});
            ach.isTrophy = item.value(JKEY_ACH_IS_TROPHY, false);
            ach.inputTime = item.value(JKEY_ACH_IS_SINGLE_SESSION, false)
                    ? AchievementInputTime::Limited
                    : AchievementInputTime::Unlimited;

            if (const auto rulesIt = item.find(JKEY_ACH_RULES);
                rulesIt != item.end() && rulesIt->is_array()) {
                for (const auto &ruleJson : *rulesIt) {
                    if (!ruleJson.is_object()) {
                        continue;
                    }
                    AchievementRule rule;
                    rule.type = ruleJson.value(JKEY_ACH_RULE_TYPE, std::string {});
                    rule.comparison = ruleJson.value(JKEY_ACH_RULE_COMPARISON, std::string {">"});
                    rule.target = ruleJson.value(JKEY_ACH_RULE_TARGET, 0);
                    rule.reference = ruleJson.value(JKEY_ACH_RULE_REFERENCE, std::string {});
                    ach.rules.push_back(std::move(rule));
                }
            }
            achievements.push_back(std::move(ach));
        }
    } catch (const std::exception &) {
        return false;
    }

    handle->gameState.debugSeedAchievements(std::move(achievements));
    return true;
}

void sb_fetch_achievement_progress(sb_game_handle_t handle, const char *user_id,
                                   sb_achievement_progress_callback_t callback, void *user_data)
{
    if (!handle || !user_id) {
        return;
    }
    handle->postApiJob(
            JobFetchAchievementProgress {handle, copyCStr(user_id), callback, user_data});
}

void sb_unlock_achievement(sb_game_handle_t handle, const char *user_id, const char *achievement_key,
                           int count, sb_achievement_unlock_callback_t callback, void *user_data)
{
    if (!handle || !user_id) {
        return;
    }
    handle->postApiJob(JobUnlockAchievement {handle, copyCStr(user_id), copyCStr(achievement_key),
                                             count, callback, user_data});
}

void sb_lock_achievement(sb_game_handle_t handle, const char *user_id, const char *achievement_key,
                         sb_achievement_unlock_callback_t callback, void *user_data)
{
    if (!handle || !user_id) {
        return;
    }
    handle->postApiJob(JobLockAchievement {handle, copyCStr(user_id), copyCStr(achievement_key),
                                           callback, user_data});
}

// ------------------------------------------------------------------------------------------------
// Definition and progress cache - synchronous, called directly on the impl
// ------------------------------------------------------------------------------------------------

bool sb_has_achievements(sb_game_handle_t handle)
{
    return handle ? handle->gameState.hasAchievements() : false;
}

size_t sb_get_cached_achievements_count(sb_game_handle_t handle)
{
    return handle ? handle->gameState.getCachedAchievements().size() : 0;
}

bool sb_get_cached_achievement_at(sb_game_handle_t handle, size_t index,
                                 sb_achievement_t *achievement)
{
    if (!handle || !achievement) {
        return false;
    }

    auto achievements = handle->gameState.getCachedAchievements();
    if (index >= achievements.size()) {
        return false;
    }

    g_scratch.achievement = std::move(achievements[index]);
    toCAchievement(g_scratch.achievement, achievement);
    return true;
}

bool sb_get_cached_achievement(sb_game_handle_t handle, const char *key,
                               sb_achievement_t *achievement)
{
    if (!handle || !key || !achievement) {
        return false;
    }

    auto found = handle->gameState.getCachedAchievement(key);
    if (!found) {
        return false;
    }

    g_scratch.achievement = std::move(*found);
    toCAchievement(g_scratch.achievement, achievement);
    return true;
}

bool sb_get_cached_progress(sb_game_handle_t handle, const char *user_id, const char *key,
                            sb_achievement_progress_t *progress)
{
    if (!handle || !user_id || !key || !progress) {
        return false;
    }

    auto found = handle->gameState.getCachedProgress(user_id, key);
    if (!found) {
        return false;
    }

    g_scratch.progress = std::move(*found);
    toCProgress(g_scratch.progress, progress);
    return true;
}

size_t sb_achievement_get_rules_count(sb_game_handle_t handle, const char *achievement_key)
{
    if (!handle || !achievement_key) {
        return 0;
    }

    const auto found = handle->gameState.getCachedAchievement(achievement_key);
    return found ? found->rules.size() : 0;
}

bool sb_achievement_get_rule_at(sb_game_handle_t handle, const char *achievement_key, size_t index,
                                sb_achievement_rule_t *rule)
{
    if (!handle || !achievement_key || !rule) {
        return false;
    }

    auto found = handle->gameState.getCachedAchievement(achievement_key);
    if (!found || index >= found->rules.size()) {
        return false;
    }

    g_scratch.rule = std::move(found->rules[index]);
    toCRule(g_scratch.rule, rule);
    return true;
}

// ------------------------------------------------------------------------------------------------
// Local matching - synchronous, called directly on the impl
// ------------------------------------------------------------------------------------------------

size_t sb_check_mode_achievements(sb_game_handle_t handle, const char *mode_name,
                                  const char *mode_type, const char *user_id, const char **keys,
                                  size_t max_keys)
{
    if (!handle || !mode_name || !mode_type || !user_id) {
        return 0;
    }
    return publishMatchedKeys(handle->gameState.checkModeAchievements(mode_name, mode_type, user_id),
                              keys, max_keys);
}

size_t sb_check_mode_achievements_with_score(sb_game_handle_t handle, const char *mode_name,
                                             const char *mode_type, const char *user_id, int64_t score,
                                             const char **keys, size_t max_keys)
{
    if (!handle || !mode_name || !mode_type || !user_id) {
        return 0;
    }
    return publishMatchedKeys(handle->gameState.checkModeAchievementsWithScore(mode_name, mode_type,
                                                                              user_id, score),
                              keys, max_keys);
}

size_t sb_check_score_achievements(sb_game_handle_t handle, int64_t score, const char *user_id,
                                   const char **keys, size_t max_keys)
{
    if (!handle || !user_id) {
        return 0;
    }
    return publishMatchedKeys(handle->gameState.checkScoreAchievements(score, user_id), keys,
                              max_keys);
}

bool sb_increment_achievement_progress(sb_game_handle_t handle, const char *key, const char *user_id,
                                       int increment, const char *metric_key)
{
    if (!handle || !key || !user_id) {
        return false;
    }
    return handle->gameState.incrementProgress(key, user_id, increment, copyCStr(metric_key));
}

void sb_set_achievement_triggered_callback(sb_game_handle_t handle,
                                           sb_achievement_triggered_callback_t callback,
                                           void *user_data)
{
    if (!handle) {
        return;
    }

    if (!callback) {
        handle->gameState.setAchievementTriggeredCallback(nullptr);
        return;
    }

    handle->gameState.setAchievementTriggeredCallback(
            [callback, user_data](const std::string &key, const std::string &userId,
                                  bool isUnlock, int progress) {
                callback(key.c_str(), userId.c_str(), isUnlock, progress, user_data);
            });
}

void sb_clear_achievement_user_progress(sb_game_handle_t handle, const char *user_id)
{
    if (!handle || !user_id) {
        return;
    }
    handle->gameState.clearUserProgress(user_id);
}

void sb_clear_achievement_progress(sb_game_handle_t handle)
{
    if (!handle) {
        return;
    }
    handle->gameState.clearAllProgress();
}

// ------------------------------------------------------------------------------------------------
// DMD frame cache
// ------------------------------------------------------------------------------------------------

void sb_download_achievement_frames(sb_game_handle_t handle)
{
    if (!handle) {
        return;
    }
    // The individual downloads are already asynchronous inside Net, so this only has to enumerate
    // the missing frames - no need to queue it.
    handle->gameState.downloadAchievementFrames();
}

bool sb_has_dmd_frame(sb_game_handle_t handle, const char *key)
{
    if (!handle || !key) {
        return false;
    }
    return handle->gameState.hasDmdFrame(key);
}

const uint8_t *sb_get_dmd_frame(sb_game_handle_t handle, const char *key, size_t *size)
{
    if (size) {
        *size = 0;
    }
    if (!handle || !key) {
        return nullptr;
    }

    g_scratch.frame = handle->gameState.getDmdFrame(key);
    if (g_scratch.frame.empty()) {
        return nullptr;
    }

    if (size) {
        *size = g_scratch.frame.size();
    }
    return g_scratch.frame.data();
}
