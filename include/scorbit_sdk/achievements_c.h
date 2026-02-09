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
 * @file achievements_c.h
 * @brief Scorbit SDK Achievements Module - C API
 *
 * This module provides C-compatible functions for working with achievements.
 * See achievements.h for a detailed overview of the achievements system.
 *
 * @section achievements_c_usage Basic Usage (C)
 *
 * @code{.c}
 * // Callback for achievements fetch
 * void on_achievements(sb_error_t error, const sb_achievement_t* achievements,
 *                      size_t count, void* user_data) {
 *     if (error == SB_EC_SUCCESS) {
 *         printf("Loaded %zu achievements\n", count);
 *         for (size_t i = 0; i < count; i++) {
 *             printf("  - %s: %s\n", achievements[i].key, achievements[i].name);
 *         }
 *     }
 * }
 *
 * // Fetch achievements
 * sb_fetch_achievements(handle, on_achievements, NULL);
 *
 * // Check for mode-based achievement matches
 * const char* matched_keys[10];
 * size_t count = sb_check_mode_achievements(handle, "Multiball", "start",
 *                                            user_id, matched_keys, 10);
 *
 * // Unlock matched achievements
 * for (size_t i = 0; i < count; i++) {
 *     sb_unlock_achievement(handle, user_id, matched_keys[i], 1,
 *                           on_unlock_result, NULL);
 * }
 * @endcode
 *
 * @see achievements.h for the C++ API and detailed documentation
 */

#pragma once

#include <scorbit_sdk/export.h>
#include <scorbit_sdk/common_types_c.h>
#include <scorbit_sdk/net_types_c.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Achievement input time type.
 *
 * Determines whether achievement progress is tracked per-session or lifetime.
 */
typedef enum {
    SB_ACH_INPUT_LIMITED = 0,   ///< Single-session achievement
    SB_ACH_INPUT_UNLIMITED = 1, ///< Lifetime achievement (progress persists)
} sb_achievement_input_time_t;

/**
 * @brief Achievement trigger type.
 */
typedef enum {
    SB_ACH_TRIGGER_GAME = 0,           ///< Triggered by game events
    SB_ACH_TRIGGER_MODE = 1,           ///< Triggered by mode start/complete
    SB_ACH_TRIGGER_SCORE = 2,          ///< Triggered by score threshold
    SB_ACH_TRIGGER_SUBACHIEVEMENT = 3, ///< Triggered by other achievements
} sb_achievement_trigger_t;

/**
 * @brief Achievement mode type for mode-based achievements.
 */
typedef enum {
    SB_ACH_MODE_NONE = 0,     ///< Not a mode-based achievement
    SB_ACH_MODE_START = 1,    ///< Triggered when mode starts
    SB_ACH_MODE_COMPLETE = 2, ///< Triggered when mode completes
    SB_ACH_MODE_STACK = 3,    ///< Triggered when modes are stacked
} sb_achievement_mode_type_t;

/**
 * @brief Achievement rule structure (v2).
 *
 * Represents a single rule condition for an achievement.
 * Strings are owned by the SDK and valid until the next achievement API call.
 */
typedef struct {
    const char *type;            ///< Rule type (e.g., "PROGRESS", "SCORE", "MODE")
    const char *comparison;      ///< Comparison operator: ">", "<", "="
    int target;                  ///< Target value for the comparison
    const char *reference;       ///< Context-dependent reference (mode name, metric key)
    int subachievement_id;       ///< Sub-achievement ID (0 if not ACHIEVEMENT type)
} sb_achievement_rule_t;

/**
 * @brief Achievement definition structure.
 *
 * Contains all the metadata about an achievement that can be earned.
 * Strings are owned by the SDK and valid until the next achievement API call.
 */
typedef struct {
    const char *key;             ///< Unique achievement key
    const char *name;            ///< Display name
    const char *description;     ///< How to earn the achievement
    int count;                   ///< Counter threshold to unlock
    const char *image_url;       ///< Image URL when achievement is visible/earned
    const char *obscure_image_url; ///< Image URL when achievement is hidden
    bool obscure;                ///< Whether to hide details until earned
    bool visible;                ///< Whether achievement is visible in lists
    bool is_trophy;              ///< Whether this is a trophy (only one holder at a time)
    bool notify_when_achieved;   ///< Whether to notify followers when earned
    sb_achievement_input_time_t input_time; ///< Limited (session) or unlimited (lifetime)
    sb_achievement_trigger_t trigger;       ///< What triggers this achievement (derived from rules)
    sb_achievement_mode_type_t mode_type;   ///< Mode type for mode-based achievements (derived from rules)
    const char *mode_name;       ///< Mode name (for mode-based triggers, derived from rules)
    int64_t target_score;        ///< Target score (for score-based triggers, derived from rules)
    int group_id;                ///< Group ID for leveled achievements
    int achievement_id;          ///< Achievement ID within group
    size_t rules_count;          ///< Number of rules for this achievement
} sb_achievement_t;

/**
 * @brief User's progress towards an achievement.
 */
typedef struct {
    const char *key;             ///< Achievement key
    int progress;                ///< Current progress count
    bool unlocked;               ///< Whether the achievement is unlocked
    const char *unlocked_at;     ///< ISO timestamp when unlocked (NULL if not unlocked)
} sb_achievement_progress_t;

/**
 * @brief Result of an unlock request.
 */
typedef struct {
    const char *key;             ///< Achievement key
    bool success;                ///< Whether the unlock was successful
    bool newly_unlocked;         ///< Whether this was a new unlock (vs already unlocked)
    const char *message;         ///< Optional message from server (may be NULL)
} sb_achievement_unlock_result_t;

/**
 * @brief Callback for fetching achievements.
 *
 * @param error Error code (SB_EC_SUCCESS on success)
 * @param achievements Array of achievement definitions
 * @param count Number of achievements in the array
 * @param user_data User data passed when making the request
 */
typedef void (*sb_achievements_callback_t)(sb_error_t error, const sb_achievement_t *achievements,
                                           size_t count, void *user_data);

/**
 * @brief Callback for fetching user achievement progress.
 *
 * @param error Error code (SB_EC_SUCCESS on success)
 * @param progress Array of user progress for each achievement
 * @param count Number of progress entries in the array
 * @param user_data User data passed when making the request
 */
typedef void (*sb_achievement_progress_callback_t)(sb_error_t error,
                                                   const sb_achievement_progress_t *progress,
                                                   size_t count, void *user_data);

/**
 * @brief Callback for unlock/lock operations.
 *
 * @param error Error code (SB_EC_SUCCESS on success)
 * @param result Result of the unlock operation
 * @param user_data User data passed when making the request
 */
typedef void (*sb_achievement_unlock_callback_t)(sb_error_t error,
                                                 const sb_achievement_unlock_result_t *result,
                                                 void *user_data);

// ------------------------------------------------------------------------------------------------
// Achievement API Functions
// ------------------------------------------------------------------------------------------------

/**
 * @brief Fetch all achievements for the current machine.
 *
 * Retrieves the list of achievement definitions for the machine associated with
 * the game state. Results are returned asynchronously via the callback.
 *
 * @param handle The game handle.
 * @param callback Callback function to receive the results.
 * @param user_data Optional user data to pass to the callback.
 */
SCORBIT_SDK_EXPORT
void sb_fetch_achievements(sb_game_handle_t handle, sb_achievements_callback_t callback,
                           void *user_data);

/**
 * @brief Fetch achievement progress for a specific user.
 *
 * Retrieves the user's progress for all achievements on the current machine.
 * Results are returned asynchronously via the callback.
 *
 * @param handle The game handle.
 * @param user_id The user ID to fetch progress for (0 for current user).
 * @param callback Callback function to receive the results.
 * @param user_data Optional user data to pass to the callback.
 */
SCORBIT_SDK_EXPORT
void sb_fetch_achievement_progress(sb_game_handle_t handle, int64_t user_id,
                                   sb_achievement_progress_callback_t callback, void *user_data);

/**
 * @brief Request to unlock an achievement for a user.
 *
 * Sends an unlock request to the server for the specified achievement and user.
 * For counter-based achievements, you can specify the count increment.
 * Results are returned asynchronously via the callback.
 *
 * @param handle The game handle.
 * @param user_id The user ID to unlock the achievement for.
 * @param achievement_key The unique key of the achievement to unlock.
 * @param count The count value (1 for boolean achievements, or increment for counters).
 * @param callback Callback function to receive the result.
 * @param user_data Optional user data to pass to the callback.
 */
SCORBIT_SDK_EXPORT
void sb_unlock_achievement(sb_game_handle_t handle, int64_t user_id, const char *achievement_key,
                           int count, sb_achievement_unlock_callback_t callback, void *user_data);

/**
 * @brief Request to lock (revoke) a trophy achievement.
 *
 * Sends a lock request to the server. This is typically used for trophy achievements
 * when the holder loses the trophy to another player.
 * Results are returned asynchronously via the callback.
 *
 * @param handle The game handle.
 * @param user_id The user ID to lock the achievement for.
 * @param achievement_key The unique key of the trophy to lock.
 * @param callback Callback function to receive the result.
 * @param user_data Optional user data to pass to the callback.
 */
SCORBIT_SDK_EXPORT
void sb_lock_achievement(sb_game_handle_t handle, int64_t user_id, const char *achievement_key,
                         sb_achievement_unlock_callback_t callback, void *user_data);

// ------------------------------------------------------------------------------------------------
// Achievement Cache and Local Matching
// ------------------------------------------------------------------------------------------------

/**
 * @brief Callback for local achievement trigger events.
 *
 * Called when local matching detects an achievement trigger or progress update.
 *
 * @param key Achievement key that was triggered
 * @param user_id User ID for whom the achievement was triggered
 * @param is_unlock true if this was an unlock event, false if progress update
 * @param progress Current progress value
 * @param user_data User data passed when registering the callback
 */
typedef void (*sb_achievement_triggered_callback_t)(const char *key, int64_t user_id, bool is_unlock,
                                                    int progress, void *user_data);

/**
 * @brief Check if achievements have been fetched and cached.
 *
 * @param handle The game handle.
 * @return true if achievements are cached, false otherwise.
 */
SCORBIT_SDK_EXPORT
bool sb_has_achievements(sb_game_handle_t handle);

/**
 * @brief Get the number of cached achievements.
 *
 * Use with sb_get_cached_achievement_at() to iterate through cached achievements.
 *
 * @param handle The game handle.
 * @return Number of cached achievements.
 */
SCORBIT_SDK_EXPORT
size_t sb_get_cached_achievements_count(sb_game_handle_t handle);

/**
 * @brief Get a cached achievement by index.
 *
 * @param handle The game handle.
 * @param index Index of the achievement (0 to count-1).
 * @param achievement Output structure to receive the achievement data.
 * @return true if achievement was found, false if index out of range.
 */
SCORBIT_SDK_EXPORT
bool sb_get_cached_achievement_at(sb_game_handle_t handle, size_t index,
                                  sb_achievement_t *achievement);

/**
 * @brief Get a cached achievement by key.
 *
 * @param handle The game handle.
 * @param key The achievement key to look up.
 * @param achievement Output structure to receive the achievement data.
 * @return true if achievement was found, false otherwise.
 */
SCORBIT_SDK_EXPORT
bool sb_get_cached_achievement(sb_game_handle_t handle, const char *key,
                               sb_achievement_t *achievement);

/**
 * @brief Get cached progress for a user and achievement.
 *
 * @param handle The game handle.
 * @param user_id The user ID to get progress for.
 * @param key The achievement key.
 * @param progress Output structure to receive the progress data.
 * @return true if progress was found in cache, false otherwise.
 */
SCORBIT_SDK_EXPORT
bool sb_get_cached_progress(sb_game_handle_t handle, int64_t user_id, const char *key,
                            sb_achievement_progress_t *progress);

/**
 * @brief Check for mode-based achievements that should trigger.
 *
 * Uses cached achievement definitions to find achievements matching
 * the mode event. Returns achievement keys via callback.
 *
 * @param handle The game handle.
 * @param mode_name The name of the mode that started/completed.
 * @param mode_type The type of mode event: "start", "complete", or "stack".
 * @param user_id The user to check progress against.
 * @param keys Output array for matched achievement keys (caller allocates).
 * @param max_keys Maximum number of keys to return.
 * @return Number of matching achievements found.
 */
SCORBIT_SDK_EXPORT
size_t sb_check_mode_achievements(sb_game_handle_t handle, const char *mode_name,
                                  const char *mode_type, int64_t user_id, const char **keys,
                                  size_t max_keys);

/**
 * @brief Check for score-based achievements that should trigger.
 *
 * Uses cached achievement definitions to find achievements matching
 * the score threshold. Returns achievement keys.
 *
 * @param handle The game handle.
 * @param score The current score to check against thresholds.
 * @param user_id The user to check progress against.
 * @param keys Output array for matched achievement keys (caller allocates).
 * @param max_keys Maximum number of keys to return.
 * @return Number of matching achievements found.
 */
SCORBIT_SDK_EXPORT
size_t sb_check_score_achievements(sb_game_handle_t handle, int64_t score, int64_t user_id,
                                   const char **keys, size_t max_keys);

/**
 * @brief Increment local progress for a counter-based achievement.
 *
 * Updates the local cache and triggers callback if achievement unlocked.
 *
 * @param handle The game handle.
 * @param key The achievement key.
 * @param user_id The user ID.
 * @param increment Amount to increment (default 1).
 * @return true if achievement was newly unlocked, false otherwise.
 */
SCORBIT_SDK_EXPORT
bool sb_increment_achievement_progress(sb_game_handle_t handle, const char *key, int64_t user_id,
                                       int increment);

/**
 * @brief Register a callback for local achievement trigger events.
 *
 * The callback is invoked when local matching detects that an achievement
 * should be triggered or when progress is incremented.
 *
 * @param handle The game handle.
 * @param callback The callback function, or NULL to clear.
 * @param user_data User data to pass to the callback.
 */
SCORBIT_SDK_EXPORT
void sb_set_achievement_triggered_callback(sb_game_handle_t handle,
                                           sb_achievement_triggered_callback_t callback,
                                           void *user_data);

// ------------------------------------------------------------------------------------------------
// Achievement Rule Accessors (v2)
// ------------------------------------------------------------------------------------------------

/**
 * @brief Get the number of rules for a cached achievement.
 *
 * @param handle The game handle.
 * @param achievement_key The achievement key to look up.
 * @return Number of rules, or 0 if achievement not found.
 */
SCORBIT_SDK_EXPORT
size_t sb_achievement_get_rules_count(sb_game_handle_t handle, const char *achievement_key);

/**
 * @brief Get a rule from a cached achievement by index.
 *
 * @param handle The game handle.
 * @param achievement_key The achievement key to look up.
 * @param index Index of the rule (0 to count-1).
 * @param rule Output structure to receive the rule data.
 * @return true if rule was found, false if index out of range or achievement not found.
 */
SCORBIT_SDK_EXPORT
bool sb_achievement_get_rule_at(sb_game_handle_t handle, const char *achievement_key, size_t index,
                                sb_achievement_rule_t *rule);

// ------------------------------------------------------------------------------------------------
// DMD Frame Cache (Internal for scorbitd)
// ------------------------------------------------------------------------------------------------

/**
 * @brief Download achievement DMD frames.
 *
 * Downloads achievement images and caches them for DMD display.
 * This is intended for internal use by scorbitd.
 *
 * @param handle The game handle.
 */
SCORBIT_SDK_EXPORT
void sb_download_achievement_frames(sb_game_handle_t handle);

/**
 * @brief Check if a DMD frame is cached for an achievement.
 *
 * @param handle The game handle.
 * @param key The achievement key.
 * @return true if frame is cached, false otherwise.
 */
SCORBIT_SDK_EXPORT
bool sb_has_dmd_frame(sb_game_handle_t handle, const char *key);

/**
 * @brief Get cached DMD frame data for an achievement.
 *
 * @param handle The game handle.
 * @param key The achievement key.
 * @param size Output parameter for the size of the frame data.
 * @return Pointer to frame data buffer, or NULL if not cached.
 *         The pointer is valid until the next DMD frame API call.
 */
SCORBIT_SDK_EXPORT
const uint8_t *sb_get_dmd_frame(sb_game_handle_t handle, const char *key, size_t *size);

#ifdef __cplusplus
}
#endif
