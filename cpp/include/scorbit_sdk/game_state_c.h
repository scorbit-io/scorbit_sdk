/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#pragma once

#include "common_types_c.h"
#include <scorbit_sdk/export.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a game state.
 *
 * Initializes and creates a game state. Use this function at the start of the application.
 * When the application terminates, call @ref sb_destroy_game_state to release the allocated
 * resources.
 *
 * @note Normally, one game state per application is sufficient.
 *
 * @return A handle to the game state, or NULL if creation fails.
 */
SCORBIT_SDK_EXPORT
sb_game_handle_t sb_create_game_state();

/**
 * @brief Destroy the game state.
 *
 * Frees the resources associated with the game state created by @ref sb_create_game_state.
 * This function should be called when the application finishes.
 *
 * @param handle The game handle created by @ref sb_create_game_state. If the handle is NULL,
 * the function does nothing.
 */
SCORBIT_SDK_EXPORT
void sb_destroy_game_state(sb_game_handle_t handle);

/**
 * @brief Mark the game as started.
 *
 * This function sets the game session active, resetting the game state. It initializes the
 * active player to Player 1 with a score of 0, and sets the current ball to 1.
 *
 * If the game is already in progress, this function has no effect.
 *
 * @note After starting the game, @ref sb_commit must be called to notify the cloud. Optionally,
 * before calling @ref sb_commit, the active player, scores, modes, or current ball can be
 * modified.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 */
SCORBIT_SDK_EXPORT
void sb_set_game_started(sb_game_handle_t handle);

/**
 * @brief Mark the game as finished.
 *
 * Marks the game as completed. Call this function when the game ends.
 *
 * @note This function automatically commits changes with @ref sb_commit.
 *
 * @warning After the game is finished, you can't add any mode or change the players' scores.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 */
SCORBIT_SDK_EXPORT
void sb_set_game_finished(sb_game_handle_t handle);

/**
 * @brief Set the current ball number.
 *
 * Updates the current ball number in the game. When the game starts, the ball number is
 * automatically set to 1.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 * @param ball The ball number [1-9]. If the ball number is out of range, the function does
 * nothing.
 */
SCORBIT_SDK_EXPORT
void sb_set_current_ball(sb_game_handle_t handle, sb_ball_t ball);

/**
 * @brief Set the active player.
 *
 * Updates the current active player in the game. By default, player 1 is the active player.
 *
 * @note If active player was set while player is not yet exists, new player will be added with
 * score 0 and set active.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 * @param player The player's number [1-9]. Typically, up to 6 players are supported in pinball.
 * If the player number is out of range, the function does nothing.
 */
SCORBIT_SDK_EXPORT
void sb_set_active_player(sb_game_handle_t handle, sb_player_t player);

/**
 * @brief Set the player's score.
 *
 * Updates the specified player's score. If the new score is the same as the current score,
 * no update is made.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 * @param player The player's number [1-9]. If the player number is out of range, the function
 * does nothing.
 * @param score The player's new score.
 */
SCORBIT_SDK_EXPORT
void sb_set_score(sb_game_handle_t handle, sb_player_t player, sb_score_t score);

/**
 * @brief Add a mode to the game.
 *
 * Adds a mode to the game's active mode list. If the mode already exists, the function skips it.
 * To remove a mode, use @ref sb_remove_mode. All modes can be cleared at once using
 * @ref sb_clear_modes.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 * @param mode The mode to add (e.g., "MB:Multiball").
 */
SCORBIT_SDK_EXPORT
void sb_add_mode(sb_game_handle_t handle, const char *mode);

/**
 * @brief Remove a mode from the game.
 *
 * Removes a mode from the game's active mode list. If the mode does not exist, the function skips
 * it. To remove all modes at once, use @ref sb_clear_modes.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 * @param mode The mode to remove (e.g., "MB:Multiball"). If the mode doesn't exist, the
 * function does nothing.
 */
SCORBIT_SDK_EXPORT
void sb_remove_mode(sb_game_handle_t handle, const char *mode);

/**
 * @brief Clear all modes.
 *
 * Removes all modes from the game's active mode list.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 */
SCORBIT_SDK_EXPORT
void sb_clear_modes(sb_game_handle_t handle);

/**
 * @brief Commit changes to the game state.
 *
 * Applies all changes made to the game state. This function should be called after
 * any modifications to the game state, such as @ref sb_set_active_player, @ref sb_set_score, @ref
 * sb_add_mode, @ref sb_remove_mode, or @ref sb_clear_modes.
 *
 * If nothing was changed, this function does nothing.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 */
SCORBIT_SDK_EXPORT
void sb_commit(sb_game_handle_t handle);

#ifdef __cplusplus
}
#endif
