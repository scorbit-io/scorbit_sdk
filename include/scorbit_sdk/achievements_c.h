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
 * @file achievements_c.h
 * @brief Achievements: definitions, live evaluation and progress.
 *
 * The SDK evaluates achievements by itself. It downloads the definitions for this machine, and on
 * every @ref sb_commit re-evaluates each achievement for every player who claimed a slot, against
 * the game state reported with @ref sb_set_score, @ref sb_add_mode, @ref sb_set_mode_completed,
 * @ref sb_add_event and the ball and player setters. Game code does not ask for evaluation.
 *
 * What happens is reported through the event callback as @ref SB_EVT_ACHIEVEMENT_UPDATED; use
 * @ref sb_event_achievement_updated to read it. The functions below read the cached definitions
 * and each player's current progress, e.g. to draw a progress display between balls.
 *
 * @note The game state is updated asynchronously, so progress read right after @ref sb_commit may
 * not include that commit yet.
 *
 * @note Strings returned through the structures below are owned by the SDK. They stay valid until
 * the next call of any function of this header from the same thread.
 */

/**
 * @brief What happened to an achievement, as reported by @ref SB_EVT_ACHIEVEMENT_UPDATED.
 *
 * An achievement the machine decides itself is reported as @ref SB_ACHIEVEMENT_UNLOCKED_LOCALLY at
 * once and may be presented immediately; the server's verdict follows as @ref
 * SB_ACHIEVEMENT_CONFIRMED, @ref SB_ACHIEVEMENT_ALREADY_HELD or @ref SB_ACHIEVEMENT_RETRACTED.
 * Trophies are never decided by the machine.
 */
typedef enum {
    /** Progress changed; reported at ball boundaries. */
    SB_ACHIEVEMENT_PROGRESS = 0,
    /** The machine decided the player earned it. It may be presented now. */
    SB_ACHIEVEMENT_UNLOCKED_LOCALLY = 1,
    /** The server granted it. */
    SB_ACHIEVEMENT_CONFIRMED = 2,
    /** The server says the player already held it: do not celebrate again. */
    SB_ACHIEVEMENT_ALREADY_HELD = 3,
    /** The server refused an unlock the machine decided: a presented unlock must be retracted. */
    SB_ACHIEVEMENT_RETRACTED = 4,
} sb_achievement_status_t;

/** @brief The twelve rule types. */
typedef enum {
    SB_ACHIEVEMENT_RULE_MODE = 0,           ///< Times the referenced mode was activated
    SB_ACHIEVEMENT_RULE_MODE_COMPLETED = 1, ///< Times the referenced mode was completed
    SB_ACHIEVEMENT_RULE_MODE_STACK = 2,     ///< Times the referenced modes were active together
    SB_ACHIEVEMENT_RULE_SCORE = 3,          ///< The player's score
    SB_ACHIEVEMENT_RULE_EVENT = 4,          ///< The value of the referenced event register
    SB_ACHIEVEMENT_RULE_SESSION = 5,        ///< Games played
    SB_ACHIEVEMENT_RULE_TIMER_SESSION = 6,  ///< Milliseconds at the machine in one game
    SB_ACHIEVEMENT_RULE_TIMER_BALL = 7,     ///< Milliseconds one ball lasted
    SB_ACHIEVEMENT_RULE_TIMER_MODE = 8,     ///< Milliseconds one activation of the mode lasted
    SB_ACHIEVEMENT_RULE_TIMER_BETWEEN = 9,  ///< Milliseconds to bring the other facts together
    SB_ACHIEVEMENT_RULE_BALL = 10,          ///< The ball the other facts had to happen on
    SB_ACHIEVEMENT_RULE_ACHIEVEMENT = 11,   ///< Whether the referenced achievement is held
} sb_achievement_rule_type_t;

/** @brief Rule predicates; all inclusive. */
typedef enum {
    SB_ACHIEVEMENT_EQ = 0, ///< Equal to the target
    SB_ACHIEVEMENT_LE = 1, ///< Less than or equal to the target
    SB_ACHIEVEMENT_GE = 2, ///< Greater than or equal to the target
    SB_ACHIEVEMENT_NE = 3, ///< Not equal to the target
} sb_achievement_comparison_t;

/** @brief The window an achievement's rules are measured over. */
typedef enum {
    SB_ACHIEVEMENT_IN_SESSION = 0, ///< The current game only
    SB_ACHIEVEMENT_UNLIMITED = 1,  ///< Every game the player has played, for life
} sb_achievement_evaluation_t;

/** @brief What an achievement attaches to. */
typedef enum {
    SB_ACHIEVEMENT_SCOPE_GAME = 0,
    SB_ACHIEVEMENT_SCOPE_VENUE = 1,
    SB_ACHIEVEMENT_SCOPE_EVENT = 2,
    SB_ACHIEVEMENT_SCOPE_GLOBAL = 3,
} sb_achievement_scope_t;

/** @brief An achievement definition. */
typedef struct {
    const char *key; ///< Immutable unique key, e.g. "game-cv-boom-balloon"
    const char *name;
    const char *description;
    sb_achievement_scope_t scope;
    sb_achievement_evaluation_t evaluation;
    bool is_trophy; ///< Held by one player at a time; decided by the server only
    bool visible;   ///< When false, an ingredient: never shown to players
    bool obscure;   ///< When true, show the obscure image and mask the text until earned
    bool notify_when_achieved;
    const char *icon_url;
    const char *obscure_image_url;
    bool has_group;           ///< Whether the achievement belongs to a group (tier ladder)
    int64_t group_id;         ///< Valid when has_group
    int64_t display_position; ///< Dense 1, 2, 3... position in the group; valid when has_group
    size_t rules_count;       ///< Use @ref sb_achievement_rule_at to read each rule
} sb_achievement_t;

/** @brief One rule of an achievement; all rules must be satisfied together. */
typedef struct {
    sb_achievement_rule_type_t type;
    sb_achievement_comparison_t comparison;
    int64_t target;
    const char *reference; ///< Mode or event name, stack list or prerequisite key; may be ""
} sb_achievement_rule_t;

/** @brief A claimed player's progress on one achievement this game. */
typedef struct {
    bool held;          ///< The player holds it (earned before, or this game)
    bool all_satisfied; ///< Every rule is currently satisfied
    bool confirmed;     ///< The server granted it this game
} sb_achievement_progress_t;

/** @brief A claimed player's progress on one rule. */
typedef struct {
    bool judged;    ///< False when the machine can't judge the rule (value is then 0)
    int64_t value;  ///< Lifetime value for an unlimited achievement, this game's otherwise
    int64_t target; ///< The rule's target, for "value of target" displays
    bool satisfied;
} sb_achievement_rule_progress_t;

// ---- Definitions ---------------------------------------------------------------------------

/**
 * @brief Number of achievement definitions cached for this machine.
 *
 * Definitions are downloaded automatically once the device is authenticated and paired, and kept
 * across reboots in the directory set with @ref sb_config_set_data_dir.
 */
SCORBIT_SDK_EXPORT
size_t sb_achievements_count(sb_game_handle_t handle);

/** @brief Reads the definition at @p index (0 to @ref sb_achievements_count - 1). */
SCORBIT_SDK_EXPORT
bool sb_achievement_at(sb_game_handle_t handle, size_t index, sb_achievement_t *achievement);

/** @brief Reads the definition with @p key; returns false if there is none. */
SCORBIT_SDK_EXPORT
bool sb_achievement_find(sb_game_handle_t handle, const char *key, sb_achievement_t *achievement);

/** @brief Reads rule @p index of the achievement @p key. */
SCORBIT_SDK_EXPORT
bool sb_achievement_rule_at(sb_game_handle_t handle, const char *key, size_t index,
                            sb_achievement_rule_t *rule);

// ---- Players' progress ---------------------------------------------------------------------

/**
 * @brief Reads @p player's progress on achievement @p key during the current game.
 *
 * Returns false when the slot is not claimed, the player's state is still being fetched, or the
 * achievement is not tracked for them (an in-session achievement they already hold).
 */
SCORBIT_SDK_EXPORT
bool sb_achievement_player_progress(sb_game_handle_t handle, sb_player_t player, const char *key,
                                    sb_achievement_progress_t *progress);

/** @brief Reads @p player's progress on rule @p index of achievement @p key. */
SCORBIT_SDK_EXPORT
bool sb_achievement_player_rule_progress(sb_game_handle_t handle, sb_player_t player,
                                         const char *key, size_t index,
                                         sb_achievement_rule_progress_t *progress);

// ---- Network ---------------------------------------------------------------------------------

/** @brief Revalidates the cached definitions with the server now (normally done at start). */
SCORBIT_SDK_EXPORT
void sb_refresh_achievements(sb_game_handle_t handle);

/**
 * @brief Fetches a player's stored achievements state, as JSON (for profile displays).
 *
 * The reply is the device progress document: `{"user_id": ..., "results": [UserAchievement...]}`,
 * listing only achievements the player has state for.
 *
 * @param handle The game handle.
 * @param user_id The player's user id (see @ref sb_event_player_id).
 * @param callback Receives the error and the JSON reply.
 * @param user_data Passed to @p callback.
 */
SCORBIT_SDK_EXPORT
void sb_fetch_player_achievements(sb_game_handle_t handle, const char *user_id,
                                  sb_string_callback_t callback, void *user_data);

/**
 * @brief Reports every changed progress value now instead of at the end of the ball.
 *
 * Unlocks are reported the moment they happen and progress at every ball boundary, so this is
 * rarely needed.
 */
SCORBIT_SDK_EXPORT
void sb_flush_achievement_reports(sb_game_handle_t handle);

// ---- DMD frames ------------------------------------------------------------------------------

/**
 * @brief Downloads the machine's DMD frame bundle if the server has a newer one.
 *
 * Done automatically with the definitions; frames are then already local when needed.
 */
SCORBIT_SDK_EXPORT
void sb_download_achievement_frames(sb_game_handle_t handle);

/**
 * @brief Reads the DMD frame of achievement @p key from the local bundle.
 *
 * @param handle The game handle.
 * @param key The achievement key.
 * @param data [OUT] The frame bytes, valid until the next call of this header's functions from
 * the same thread.
 * @param size [OUT] Their size.
 * @return false if the bundle has no frame for @p key.
 */
SCORBIT_SDK_EXPORT
bool sb_achievement_frame(sb_game_handle_t handle, const char *key, const uint8_t **data,
                          size_t *size);

#ifdef __cplusplus
}
#endif
