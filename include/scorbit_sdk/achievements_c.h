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
 * See achievements.h for the C++ type definitions and an overview of the achievements system.
 *
 * @section achievements_c_engines Two engines, one authority
 *
 * Achievement definitions are stored on the Scorbit platform; evaluation is split. Rules of type
 * `"MODE"`, `"MODE_START"`, `"MODE_STACK"` and `"SCORE"` can be evaluated on the machine during
 * play. Rules of type `"PROGRESS"` and `"ACHIEVEMENT"` are evaluated by the server after the
 * session uploads. The on-machine engine is therefore **predictive only** - use it to light a
 * "you're close" indicator or to pre-load artwork, and wait for the server's `AchievementUnlocked`
 * event before telling the player they earned anything.
 *
 * All of an achievement's rules are ANDed. There is no OR.
 *
 * @section achievements_c_usage Basic Usage (C)
 *
 * @code{.c}
 * static void on_achievements(sb_error_t error, const sb_achievement_t *achievements,
 *                             size_t count, void *user_data)
 * {
 *     (void)user_data;
 *     if (error != SB_EC_SUCCESS) {
 *         return;
 *     }
 *     printf("Loaded %zu achievements\n", count);
 * }
 *
 * // 1. Fetch the definitions for this machine once at startup.
 * sb_fetch_achievements(handle, on_achievements, NULL);
 *
 * // 2. When a mode completes, ask which achievements might qualify. Nothing is evaluated
 * //    unless you ask.
 * const char *matched[16];
 * size_t n = sb_check_mode_achievements_with_score(handle, "Grand Finale", "complete",
 *                                                  user_id, current_score, matched, 16);
 *
 * // 3. Post each match; the server is the authority.
 * for (size_t i = 0; i < n; ++i) {
 *     sb_unlock_achievement(handle, user_id, matched[i], 1, on_unlock_result, NULL);
 * }
 * @endcode
 *
 * @see achievements.h for the C++ types and the shared documentation
 */

#pragma once

#include <scorbit_sdk/common_types_c.h>
#include <scorbit_sdk/export.h>
#include <scorbit_sdk/net_types_c.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Whether achievement progress is session-scoped or lifetime.
 *
 * Mirrors the server's `is_single_session` flag. `SB_ACH_INPUT_LIMITED` progress is expected to be
 * reset by the integrator when the session ends; see `sb_check_mode_achievements` for how this
 * affects re-checking.
 */
typedef enum {
    SB_ACH_INPUT_LIMITED = 0,   ///< Single-session achievement; progress resets each session
    SB_ACH_INPUT_UNLIMITED = 1, ///< Lifetime achievement; progress persists across sessions
} sb_achievement_input_time_t;

/**
 * @brief Achievement trigger type.
 *
 * Derived from `rules[0]` only, and therefore lossy for multi-rule achievements. Read the rules
 * via `sb_achievement_get_rule_at()` rather than trusting this field.
 */
typedef enum {
    SB_ACH_TRIGGER_GAME = 0,           ///< Triggered by game events
    SB_ACH_TRIGGER_MODE = 1,           ///< Triggered by mode start/complete/stack
    SB_ACH_TRIGGER_SCORE = 2,          ///< Triggered by a score threshold
    SB_ACH_TRIGGER_SUBACHIEVEMENT = 3, ///< Triggered by another achievement being earned
} sb_achievement_trigger_t;

/**
 * @brief Mode type for mode-based achievements.
 *
 * Derived from `rules[0]` only, and therefore lossy for multi-rule achievements.
 */
typedef enum {
    SB_ACH_MODE_NONE = 0,     ///< Not a mode-based achievement
    SB_ACH_MODE_START = 1,    ///< Triggered when the mode starts
    SB_ACH_MODE_COMPLETE = 2, ///< Triggered when the mode completes
    SB_ACH_MODE_STACK = 3,    ///< Triggered when modes are stacked
} sb_achievement_mode_type_t;

/**
 * @brief A single rule within an achievement definition.
 *
 * An achievement's rules are always ANDed. Strings are owned by the SDK and remain valid until the
 * next achievement API call.
 */
typedef struct {
    /**
     * Rule type. One of `"MODE"`, `"MODE_START"`, `"MODE_STACK"`, `"SCORE"`, `"PROGRESS"`,
     * `"ACHIEVEMENT"`, `"GAME_CODE"`, `"TIMER"`, `"EVENT"`, `"ATTEMPT"`.
     */
    const char *type;

    /**
     * Comparison operator: `">"`, `"<"` or `"="`. Defaults to `">"` when the server omits it.
     * Note `">"` is a **strict** greater-than, matching the server.
     */
    const char *comparison;

    /**
     * Target value the live value is compared against.
     *
     * `int64_t` because a `"SCORE"` rule's target is a pinball score, and scores routinely run
     * past 2,147,483,647. The server's `Rule.target` is still a 32-bit
     * `PositiveIntegerField`, so today nothing above that ceiling reaches the device - the wider
     * type is here so that when the API widens the field the SDK does not silently truncate, and
     * so that locally seeded definitions can express a real score target.
     */
    int64_t target;

    /**
     * Context-dependent reference: the mode name for `"MODE"` / `"MODE_START"` / `"MODE_STACK"`,
     * the metric key for `"PROGRESS"`, otherwise unused.
     */
    const char *reference;

    /** Sub-achievement FK id for `"ACHIEVEMENT"` rules; 0 otherwise. */
    int subachievement_id;
} sb_achievement_rule_t;

/**
 * @brief Achievement definition.
 *
 * Strings are owned by the SDK and remain valid until the next achievement API call.
 */
typedef struct {
    const char *key;               ///< Unique, immutable achievement key
    const char *name;              ///< Display name
    const char *description;       ///< How to earn the achievement

    /**
     * What this achievement attaches to: `"game"`, `"venue"`, `"event"`, or `"global"`.
     * `"global"` achievements are always server-evaluated regardless of rule types - see
     * @ref scorbit::Achievement::scope for the full explanation.
     */
    const char *scope;

    const char *image_url;         ///< Image URL shown when visible/earned
    const char *obscure_image_url; ///< Placeholder image URL shown while still hidden
    bool obscure;                  ///< Listed, but details replaced by a placeholder until earned
    bool visible;                  ///< Whether the achievement appears in player-facing lists
    bool is_trophy;                ///< Trophy: held by exactly one player at a time
    bool notify_when_achieved;     ///< Whether followers are notified when earned

    /** Session-scoped or lifetime; mirrors the server's `is_single_session`. */
    sb_achievement_input_time_t input_time;

    /** Derived from `rules[0]` only - lossy for multi-rule achievements. */
    sb_achievement_trigger_t trigger;

    /** Derived from `rules[0]` only - lossy for multi-rule achievements. */
    sb_achievement_mode_type_t mode_type;

    /** Derived from `rules[0]` only - lossy for multi-rule achievements. */
    const char *mode_name;

    /** Derived from `rules[0]` only - lossy for multi-rule achievements. */
    int64_t target_score;

    /** Group id for tier ladders. Groups are display-only and carry no evaluation semantics. */
    int group_id;

    /** Sparse level within the group, ordering the tier ladder. */
    int level;

    /**
     * The server's `ball_count`: a "complete this before ball N" qualifier.
     *
     * This is **not** a counter threshold, and nothing evaluates it in either engine today. A
     * counter achievement's threshold lives on its `"PROGRESS"` rule's `target`. Do not use this
     * field to decide whether an achievement is unlocked.
     */
    int ball_count;

    /** Number of rules; iterate with `sb_achievement_get_rule_at()`. */
    size_t rules_count;
} sb_achievement_t;

/**
 * @brief A user's progress towards one achievement.
 */
typedef struct {
    const char *key;         ///< Achievement key
    int progress;            ///< Accumulated counter value
    bool unlocked;           ///< Whether the achievement is currently held
    const char *unlocked_at; ///< ISO timestamp when unlocked; NULL if not unlocked
} sb_achievement_progress_t;

/**
 * @brief Result of an unlock or lock request.
 */
typedef struct {
    const char *key;     ///< Achievement key
    bool success;        ///< Whether the request succeeded
    bool newly_unlocked; ///< Whether this call was the one that unlocked it
    const char *message; ///< Optional message from the server; may be NULL
} sb_achievement_unlock_result_t;

/**
 * @brief Callback delivering fetched achievement definitions.
 *
 * On success @p achievements points to @p count entries, valid only for the duration of the
 * callback; do not retain the pointer. On failure @p achievements is NULL and @p count is 0.
 */
typedef void (*sb_achievements_callback_t)(sb_error_t error, const sb_achievement_t *achievements,
                                           size_t count, void *user_data);

/**
 * @brief Callback delivering a user's fetched achievement progress.
 *
 * On success @p progress points to @p count entries, valid only for the duration of the callback;
 * do not retain the pointer. On failure @p progress is NULL and @p count is 0.
 */
typedef void (*sb_achievement_progress_callback_t)(sb_error_t error,
                                                   const sb_achievement_progress_t *progress,
                                                   size_t count, void *user_data);

/**
 * @brief Callback delivering the result of an unlock or lock request.
 *
 * On success @p result is non-NULL and valid only for the duration of the callback. On failure it
 * is NULL.
 */
typedef void (*sb_achievement_unlock_callback_t)(sb_error_t error,
                                                 const sb_achievement_unlock_result_t *result,
                                                 void *user_data);

/**
 * @brief Callback invoked when local matching records progress or a local unlock.
 *
 * @param key Achievement key
 * @param user_id User the event applies to
 * @param is_unlock true if this call crossed the unlock threshold, false for a progress update
 * @param progress Accumulated counter value after the update
 * @param user_data User data passed when registering the callback
 *
 * The SDK holds no internal lock while this runs, so calling back into the achievement API from
 * here is safe. Still return promptly - queue artwork and animation rather than blocking.
 */
typedef void (*sb_achievement_triggered_callback_t)(const char *key, const char *user_id,
                                                    bool is_unlock, int progress, void *user_data);

// ------------------------------------------------------------------------------------------------
// Network
// ------------------------------------------------------------------------------------------------

/**
 * @brief Fetch all published achievement definitions for the current machine.
 *
 * On success the definitions are cached for local matching. Results arrive via @p callback.
 */
SCORBIT_SDK_EXPORT
void sb_fetch_achievements(sb_game_handle_t handle, sb_achievements_callback_t callback,
                           void *user_data);

/**
 * TEMPORARY - LOCAL TESTING ONLY - DELETE BEFORE COMMIT.
 *
 * Seeds the cache directly from a JSON string shaped like the real
 * GET .../scorbitron/ response, bypassing the network entirely. For local dev when no signed
 * key.json is available for real authentication.
 *
 * @return true if the JSON parsed successfully.
 */
SCORBIT_SDK_EXPORT
bool sb_debug_seed_achievements(sb_game_handle_t handle, const char *json);

/**
 * @brief Fetch one user's progress for the current machine's achievements.
 *
 * On success the progress is cached for local matching. Call this when a player claims a slot.
 *
 * @param user_id The user's UUID (the public "id" the v2 API exposes everywhere, not an
 *                internal integer id) to fetch progress for.
 */
SCORBIT_SDK_EXPORT
void sb_fetch_achievement_progress(sb_game_handle_t handle, const char *user_id,
                                   sb_achievement_progress_callback_t callback, void *user_data);

/**
 * @brief Request that the server unlock an achievement for a user.
 *
 * The server validates the request and is the authority; a local match is not an unlock.
 *
 * @param count Count value: 1 for boolean achievements, or the increment for counters.
 */
SCORBIT_SDK_EXPORT
void sb_unlock_achievement(sb_game_handle_t handle, const char *user_id, const char *achievement_key,
                           int count, sb_achievement_unlock_callback_t callback, void *user_data);

/**
 * @brief Request that the server revoke a trophy from its current holder.
 */
SCORBIT_SDK_EXPORT
void sb_lock_achievement(sb_game_handle_t handle, const char *user_id, const char *achievement_key,
                         sb_achievement_unlock_callback_t callback, void *user_data);

// ------------------------------------------------------------------------------------------------
// Definition and progress cache
// ------------------------------------------------------------------------------------------------

/**
 * @brief Return true if achievement definitions have been fetched and cached.
 */
SCORBIT_SDK_EXPORT
bool sb_has_achievements(sb_game_handle_t handle);

/**
 * @brief Return the number of cached achievement definitions.
 */
SCORBIT_SDK_EXPORT
size_t sb_get_cached_achievements_count(sb_game_handle_t handle);

/**
 * @brief Read a cached achievement by index.
 *
 * @return true if @p index was in range, false otherwise.
 */
SCORBIT_SDK_EXPORT
bool sb_get_cached_achievement_at(sb_game_handle_t handle, size_t index,
                                 sb_achievement_t *achievement);

/**
 * @brief Read a cached achievement by key.
 *
 * @return true if the key was found, false otherwise.
 */
SCORBIT_SDK_EXPORT
bool sb_get_cached_achievement(sb_game_handle_t handle, const char *key,
                               sb_achievement_t *achievement);

/**
 * @brief Read a user's cached progress for one achievement.
 *
 * @return true if a progress entry was cached, false otherwise.
 */
SCORBIT_SDK_EXPORT
bool sb_get_cached_progress(sb_game_handle_t handle, const char *user_id, const char *key,
                            sb_achievement_progress_t *progress);

/**
 * @brief Return the number of rules on a cached achievement, or 0 if the key is unknown.
 */
SCORBIT_SDK_EXPORT
size_t sb_achievement_get_rules_count(sb_game_handle_t handle, const char *achievement_key);

/**
 * @brief Read one rule of a cached achievement by index.
 *
 * @return true if the achievement and @p index were both found, false otherwise.
 */
SCORBIT_SDK_EXPORT
bool sb_achievement_get_rule_at(sb_game_handle_t handle, const char *achievement_key, size_t index,
                                sb_achievement_rule_t *rule);

// ------------------------------------------------------------------------------------------------
// Local matching
// ------------------------------------------------------------------------------------------------

/**
 * @brief Find achievements matched by a mode event, ignoring score.
 *
 * Matching is **conservative**: an achievement is reported only when every rule that this context
 * can judge is satisfied *and* no rule was left unjudged. Because no score is supplied here, an
 * achievement carrying a `"SCORE"` rule can never match through this function - use
 * `sb_check_mode_achievements_with_score()` for those. `"PROGRESS"` and `"ACHIEVEMENT"` rules are
 * server-owned and are skipped rather than counted against the match.
 *
 * Nothing is evaluated unless you call this. A match is predictive, not an unlock.
 *
 * @param mode_name The mode that started, completed, or stacked.
 * @param mode_type One of `"start"`, `"complete"`, `"stack"`.
 * @param user_id The user whose cached progress gates re-checking.
 * @param keys Caller-allocated output array for matched keys. Pointers remain valid until the next
 *             achievement API call.
 * @param max_keys Capacity of @p keys.
 * @return Number of keys written to @p keys.
 */
SCORBIT_SDK_EXPORT
size_t sb_check_mode_achievements(sb_game_handle_t handle, const char *mode_name,
                                  const char *mode_type, const char *user_id, const char **keys,
                                  size_t max_keys);

/**
 * @brief Find achievements matched by a mode event, also judging score rules.
 *
 * Same conservative semantics as `sb_check_mode_achievements()`, but `"SCORE"` rules are judged
 * against @p score instead of being treated as unevaluable. Use this entry point for any
 * achievement that combines mode and score conditions.
 */
SCORBIT_SDK_EXPORT
size_t sb_check_mode_achievements_with_score(sb_game_handle_t handle, const char *mode_name,
                                             const char *mode_type, const char *user_id, int64_t score,
                                             const char **keys, size_t max_keys);

/**
 * @brief Find achievements matched by the current score alone.
 *
 * Matching is **conservative**: mode rules cannot be judged without a mode event, so an
 * achievement combining a `"MODE"` rule with a `"SCORE"` rule is *not* reported here even once the
 * score threshold is crossed. Check those through `sb_check_mode_achievements_with_score()` when
 * the mode event arrives. `"PROGRESS"` and `"ACHIEVEMENT"` rules are skipped as server-owned.
 *
 * @return Number of keys written to @p keys.
 */
SCORBIT_SDK_EXPORT
size_t sb_check_score_achievements(sb_game_handle_t handle, int64_t score, const char *user_id,
                                   const char **keys, size_t max_keys);

/**
 * @brief Add to a counter achievement's locally accumulated progress.
 *
 * The unlock threshold is taken from the achievement's `"PROGRESS"` rule `target`, compared with
 * that rule's `comparison` operator. There is no achievement-level counter threshold in the
 * platform; `ball_count` means something unrelated and must not be used for this.
 *
 * @param metric_key The `"PROGRESS"` rule `reference` identifying which counter is being bumped.
 *                   Pass NULL or "" when the achievement has exactly one `"PROGRESS"` rule.
 * @return true only if this call crossed the threshold of a located `"PROGRESS"` rule. If no
 *         matching rule exists the progress is still recorded but false is returned, leaving the
 *         unlock decision to the server.
 */
SCORBIT_SDK_EXPORT
bool sb_increment_achievement_progress(sb_game_handle_t handle, const char *key, const char *user_id,
                                       int increment, const char *metric_key);

/**
 * @brief Register the callback invoked when local matching records progress or a local unlock.
 *
 * @param callback The callback, or NULL to clear it.
 */
SCORBIT_SDK_EXPORT
void sb_set_achievement_triggered_callback(sb_game_handle_t handle,
                                           sb_achievement_triggered_callback_t callback,
                                           void *user_data);

/**
 * @brief Drop one user's locally accumulated progress.
 *
 * Call this when a player leaves the session, so that session-scoped
 * (`SB_ACH_INPUT_LIMITED`) counters do not carry into the next session and unlock early.
 */
SCORBIT_SDK_EXPORT
void sb_clear_achievement_user_progress(sb_game_handle_t handle, const char *user_id);

/**
 * @brief Drop every user's locally accumulated progress.
 *
 * Call this on game end or session teardown.
 */
SCORBIT_SDK_EXPORT
void sb_clear_achievement_progress(sb_game_handle_t handle);

// ------------------------------------------------------------------------------------------------
// DMD frame cache (intended for scorbitd)
// ------------------------------------------------------------------------------------------------

/**
 * @brief Download and cache achievement DMD frames for later display.
 */
SCORBIT_SDK_EXPORT
void sb_download_achievement_frames(sb_game_handle_t handle);

/**
 * @brief Return true if a DMD frame is cached for @p key.
 */
SCORBIT_SDK_EXPORT
bool sb_has_dmd_frame(sb_game_handle_t handle, const char *key);

/**
 * @brief Get the cached DMD frame for @p key.
 *
 * @param size Receives the frame size in bytes.
 * @return Pointer to the frame data, valid until the next DMD frame API call, or NULL if the frame
 *         is not cached.
 */
SCORBIT_SDK_EXPORT
const uint8_t *sb_get_dmd_frame(sb_game_handle_t handle, const char *key, size_t *size);

#ifdef __cplusplus
}
#endif
