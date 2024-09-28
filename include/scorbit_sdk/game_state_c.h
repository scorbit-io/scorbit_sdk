/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2024
 *
 ****************************************************************************/

#pragma once

#include "scorbit_sdk/export.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int sb_player_num_t;
typedef int64_t sb_score_t;

struct sb_game_state_struct;
typedef struct sb_game_state_struct *sb_game_handle_t;

/**
 * @brief Status codes for API functions
 */
typedef enum {
    SB_SUCCESS = 0,
    SB_ERROR_UNKNOWN,
    SB_ERROR_INVALID_HANDLE,
    SB_ERROR_INVALID_PLAYER,
    SB_ERROR_MODE_NOT_FOUND
} sb_status_t;

/**
 * @brief Create game state
 *
 * Create one game state at the start of the application. When the application finishes, call @ref
 * sb_destroy_game_state to release resources.
 *
 * @return handle to the game state, or NULL if creation fails
 */
SCORBIT_SDK_EXPORT
sb_game_handle_t sb_create_game_state();

/**
 * @brief Destroy game state
 *
 * When the application finishes, call this function to free resources created by @ref
 * sb_create_game_state.
 *
 * @warning Do not call this function with a handle that was not created by @ref
 * sb_create_game_state. It will be undefined behavior.
 *
 * @param handle game handle created by @ref sb_create_game_state. If handle is NULL, the function
 * does nothing.
 */
SCORBIT_SDK_EXPORT
void sb_destroy_game_state(sb_game_handle_t handle);

/**
 * @brief Game started
 *
 * Call this function when the game starts. This sets the game as active.
 *
 * @param handle game handle created by @ref sb_create_game_state
 * @return SB_SUCCESS if the operation is successful, otherwise an error status
 */
SCORBIT_SDK_EXPORT
sb_status_t sb_set_game_started(sb_game_handle_t handle);

/**
 * @brief Game finished
 *
 * Call this function when the game finishes.
 *
 * @param handle game handle created by @ref sb_create_game_state
 * @return SB_SUCCESS if the operation is successful, otherwise an error status
 */
SCORBIT_SDK_EXPORT
sb_status_t sb_set_game_finished(sb_game_handle_t handle);

/**
 * @brief Set active player
 *
 * Call this function when the current player changes. The default active player is player 1.
 *
 * @param handle game handle created by @ref sb_create_game_state
 * @param player player number (1, 2, 3, 4, ...)
 * @return SB_SUCCESS if the operation is successful, or SB_ERROR_INVALID_PLAYER if the player is
 * invalid
 */
SCORBIT_SDK_EXPORT
sb_status_t sb_set_active_player(sb_game_handle_t handle, sb_player_num_t player);

/**
 * @brief Set player's score
 *
 * Sets the given player's score. If the new score matches the player's current score, the score
 * will not be updated.
 *
 * @param handle game handle created by @ref sb_create_game_state
 * @param player player number (1, 2, 3, 4, ...)
 * @param score player's score
 * @return SB_SUCCESS if the operation is successful, or SB_ERROR_INVALID_PLAYER if the player is
 * invalid
 */
SCORBIT_SDK_EXPORT
sb_status_t sb_set_score(sb_game_handle_t handle, sb_player_num_t player, sb_score_t score);

/**
 * @brief Add mode
 *
 * Adds a new mode to the modes list. If the mode already exists, it will be skipped.
 * When it's time to remove a mode, call @ref sb_remove_mode.
 * All modes can be cleared at once by @ref sb_clear_all_modes.
 *
 * @param handle game handle created by @ref sb_create_game_state
 * @param mode current mode (e.g., "MB:Multiball")
 * @return SB_SUCCESS if the operation is successful, SB_HANDLE_INVALID if the handle is invalid.
 *         SB_ERROR_MODE_ALREADY_EXISTS if the mode already exists.
 */
SCORBIT_SDK_EXPORT
sb_status_t sb_add_mode(sb_game_handle_t handle, const char *mode);

/**
 * @brief Remove mode
 *
 * Removes a mode from the modes list. If the mode doesn't exist, it will be skipped.
 * All modes can be cleared at once by @ref sb_clear_all_modes.
 *
 * @param handle game handle created by @ref sb_create_game_state
 * @param mode current mode (e.g., "MB:Multiball")
 * @return SB_SUCCESS if the operation is successful, SB_HANDLE_INVALID if the handle is invalid.
 *         SB_ERROR_MODE_NOT_FOUND if the mode does not exist.
 */
SCORBIT_SDK_EXPORT
sb_status_t sb_remove_mode(sb_game_handle_t handle, const char *mode);

/**
 * @brief Clear all modes
 *
 * Removes all modes from the list.
 *
 * @param handle game handle created by @ref sb_create_game_state
 * @return SB_SUCCESS if the operation is successful, otherwise an error status
 */
SCORBIT_SDK_EXPORT
sb_status_t sb_clear_all_modes(sb_game_handle_t handle);

#ifdef __cplusplus
}
#endif
