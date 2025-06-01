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

#pragma once

#include <scorbit_sdk/export.h>
#include "common_types_c.h"
#include "net_types_c.h"

#include <stdbool.h>
#include <stdint.h>

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
 * There are two versions: first version is @ref sb_create_game_state, which requires a signer
 * callback function and device information. The second version is @ref sb_create_game_state2,
 * which requires an encrypted key instead of a signer callback.
 *
 * @note The first version with a signer callback is intended for machines that use TPM for signing.
 * The second version is recommended for machines that do not use TPM.
 *
 * @note Normally, one game state per application is sufficient.
 *
 * @param signer The callback function to sign the game state. The function should have the
 * signature specified by @ref sb_signer_callback_t. @see examples/c_example/main.c
 * @param signer_user_data The user data passed to the signer. It can be used to pass the context.
 * @param device_info The device information used to authenticate
 *
 * @return A handle to the game state, or NULL if creation fails.
 */
SCORBIT_SDK_EXPORT
sb_game_handle_t sb_create_game_state(sb_signer_callback_t signer, void *signer_user_data,
                                      const sb_device_info_t *device_info);

SCORBIT_SDK_EXPORT
sb_game_handle_t sb_create_game_state2(const char *encrypted_key,
                                       const sb_device_info_t *device_info);

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
 * @param feature The score feature (i.e. what game feature caused score bump, like spinner, etc.).
 * If the feature is not set, it is 0.
 */
SCORBIT_SDK_EXPORT
void sb_set_score(sb_game_handle_t handle, sb_player_t player, sb_score_t score,
                  sb_score_feature_t feature);

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

// ----------------------------------------------------------------

/**
 * @brief Retrieves the current authentication status.
 *  * Key statuses to consider:
 * - @ref SB_NET_AUTHENTICATED_UNPAIRED: Authentication succeeded, but pairing is not
 * established.
 * - @ref SB_NET_AUTHENTICATED_PAIRED: Authentication succeeded, and pairing is established.
 * - @ref SB_NET_AUTHENTICATION_FAILED: The authentication process failed, indicating a
 * signing error.
 *
 *  * @return The current authentication status as an @ref sb_auth_status_t value.
 */
SCORBIT_SDK_EXPORT
sb_auth_status_t sb_get_status(sb_game_handle_t handle);

/**
 * @brief Retrieve the machine's UUID.
 *
 * If the machine UUID was not explicitly provided, it will be derived from the MAC address.
 *
 * @note The pointer to the string remains valid until this function is called again or the handle
 * is destroyed.
 *
 * @param handle The game handle created using @ref sb_create_game_state.
 * @return A pointer to the machine's UUID string.
 */
SCORBIT_SDK_EXPORT
const char *sb_get_machine_uuid(sb_game_handle_t handle);

/**
 * @brief Retrieve the pairing deeplink.
 *
 * @note The pointer to the string remains valid until this function is called again or the handle
 * is destroyed.
 *
 * @param handle The game handle created using @ref sb_create_game_state.
 * @return A pointer to the pairing deeplink string. If the machine is not paired or the SDK is not
 * yet authenticated, an empty string is returned.
 */
SCORBIT_SDK_EXPORT
const char *sb_get_pair_deeplink(sb_game_handle_t handle);

/**
 * @brief Retrieve the claim and navigation deeplink.
 *
 * @note The returned string pointer remains valid until this function is called again or the handle
 * is destroyed.
 *
 * @param handle The game handle created using @ref sb_create_game_state.
 * @param player The player number (starting from 1).
 * @return A pointer to the claim deeplink string. If the machine is not paired or the SDK is not
 * yet authenticated, an empty string is returned.
 */
SCORBIT_SDK_EXPORT
const char *sb_get_claim_deeplink(sb_game_handle_t handle, int player);

/**
 * @brief Retrieves the top scores from the leaderboard.
 *
 * @note The callback function is invoked asynchronously when the operation completes, running in
 * a separate thread from the main calling thread. It is recommended to use appropriate locks (e.g.,
 * a mutex) when accessing shared data.
 *
 * @param handle The game handle created using @ref sb_create_game_state.
 * @param score_filter A score value used to filter the leaderboard results. If a score is provided,
 * the function retrieves the ten scores above and ten scores below the specified value, allowing
 * the user to view their score in the leaderboard context. Set to 0 to disable the score filter.
 * @param callback A callback function of @ref sb_string_callback_t that receives the top scores in
 * JSON format as a string. Returns @ref SB_EC_SUCCESS if the request was successful.
 * Otherwise, it returns an error codes: @ref SB_EC_NOT_PAIRED if machine is not paired, or @ref
 * SB_EC_API_ERROR if the API call failed.
 * @param user_data Optional user data to pass to the callback. Pass NULL if not used.
 */
SCORBIT_SDK_EXPORT
void sb_request_top_scores(sb_game_handle_t handle, sb_score_t score_filter,
                           sb_string_callback_t callback, void *user_data);

/**
 * @brief Request a pairing short code (6 alphanumeric characters).
 *
 * Requests a pairing short code from the server. The short code is used to pair the device with
 * the Scorbit service where on machines which can display only aplhanumric characters. This is
 * alternative to @ref sb_get_pair_deeplink.
 *
 * @note The callback function is invoked asynchronously when the operation completes, running in
 * a separate thread from the main calling thread. It is recommended to use appropriate locks (e.g.,
 * a mutex) when accessing shared data.
 *
 * @param handle The game handle created using @ref sb_create_game_state.
 * @param callback A callback function of @ref sb_string_callback_t that receives the short code.
 * Returns @ref SB_EC_SUCCESS if the request was successful. Otherwise, it returns an error code:
 * @ref SB_EC_API_ERROR if the API call failed.
 * @param user_data Optional user data to pass to the callback. Pass NULL if not used.
 */
SCORBIT_SDK_EXPORT
void sb_request_pair_code(sb_game_handle_t handle, sb_string_callback_t callback, void *user_data);

/**
 * @brief Request to unpair a device.
 *
 * Sends a request to unpair the device from the Scorbit service. This function should be called
 * when the device is being reset by a (new) owner.
 *
 * The returned data string is the raw reply from the API and can be safely ignored. On a successful
 * unpairing, it will return @ref SB_EC_SUCCESS.
 *
 * @note The callback function is invoked asynchronously when the operation completes, running in
 * a separate thread from the main calling thread. It is recommended to use appropriate locks (e.g.,
 * a mutex) when accessing shared data.
 *
 * @param handle The game handle created using @ref sb_create_game_state.
 * @param callback A callback function of type @ref sb_string_callback_t to receive the error code.
 * Returns @ref SB_EC_SUCCESS if the request is successful, or an error code otherwise:
 * @ref SB_EC_API_ERROR if the API call fails.
 * @param user_data Optional user data to pass to the callback. Pass NULL if not needed.
 */
SCORBIT_SDK_EXPORT
void sb_request_unpair(sb_game_handle_t handle, sb_string_callback_t callback, void *user_data);

// -------------------------- PLAYER PROFILE INFO --------------------------------------

/**
 * @brief Checks whether any player profiles have been updated and prepares data for retrieval.
 *
 * @note Calling this function clears the internal update status and prepares updated player data
 * for retrieval. If no changes are made to any player's information before the next call,
 * this function will return false.
 *
 * If the function returns true, use the following functions to retrieve the updated data:
 * @ref sb_get_player_id, @ref sb_get_player_preferred_name, @ref sb_get_player_name, @ref
 * sb_get_player_initials, @ref sb_get_player_picture_url, and @ref sb_get_player_picture.
 *
 * @param handle A game handle created using @ref sb_create_game_state.
 * @return true if there are updates to any player profiles; false otherwise.
 */
SCORBIT_SDK_EXPORT
bool sb_is_players_info_updated(sb_game_handle_t handle);

/**
 * @brief Checks if player information is available. For a given player it will be available if
 * player claimed the slot.
 *
 * @param handle A game handle created using @ref sb_create_game_state.
 * @param player The player number (starting from 1).
 * @return true if player information is available; false otherwise.
 */
SCORBIT_SDK_EXPORT
bool sb_has_player_info(sb_game_handle_t handle, sb_player_t player);

/**
 * @brief Retrieves the player's ID.
 *
 * Returns the player's unique ID, which is used to identify the player in the Scorbit system.
 * The ID is a 64-bit integer. If no player information is available (i.e.,
 * @ref sb_has_player_info returned false), this function returns -1.
 *
 * @param handle A game handle created using @ref sb_create_game_state.
 * @param player The player number (starting from 1).
 * @return The player's ID, or -1 if no player info is available.
 */
SCORBIT_SDK_EXPORT
int64_t sb_get_player_id(sb_game_handle_t handle, sb_player_t player);

/**
 * @brief Retrieves the player's preferred display name.
 *
 * Returns the player's preferred display name, which is either their initials or full name,
 * depending on the player's preference.
 *
 * @warning Copy the returned string to your own buffer. The returned pointer remains valid only
 * until @ref sb_is_players_info_updated is called again.
 *
 * @param handle A game handle created using @ref sb_create_game_state.
 * @param player The player number (starting from 1).
 * @return A pointer to the player's preferred name string, or NULL if no player info is available
 * (i.e. @ref sb_has_player_info returned false).
 */
SCORBIT_SDK_EXPORT
const char *sb_get_player_preferred_name(sb_game_handle_t handle, sb_player_t player);

/**
 * @brief Retrieves the player's name.
 *
 * @warning Copy the returned string to your own buffer. The returned pointer remains valid only
 * until @ref sb_is_players_info_updated is called again.
 *
 * @param handle A game handle created using @ref sb_create_game_state.
 * @param player The player number (starting from 1).
 * @return A pointer to the player's name string, or NULL if no player info is available
 * (i.e. @ref sb_has_player_info returned false).
 */
SCORBIT_SDK_EXPORT
const char *sb_get_player_name(sb_game_handle_t handle, sb_player_t player);

/**
 * @brief Retrieves the player's initials.
 *
 * @warning Copy the returned string to your own buffer. The returned pointer remains valid only
 * until @ref sb_is_players_info_updated is called again.
 *
 * @param handle A game handle created using @ref sb_create_game_state.
 * @param player The player number (starting from 1).
 * @return A pointer to the player's initials string, or NULL if no player info is available
 * (i.e. @ref sb_has_player_info returned false).
 */
SCORBIT_SDK_EXPORT
const char *sb_get_player_initials(sb_game_handle_t handle, sb_player_t player);

/**
 * @brief Retrieves the player's profile picture URL.
 *
 * Returns the URL of the player's profile picture. SDK can automatically download picture,
 * see @ref sb_device_info_t::auto_download_player_pics, @ref sb_get_player_picture.
 *
 * @warning Copy the returned string to your own buffer. The returned pointer remains valid only
 * until @ref sb_is_players_info_updated is called again.
 *
 * @param handle A game handle created using @ref sb_create_game_state.
 * @param player The player number (starting from 1).
 * @return A pointer to the player's profile picture URL string, or NULL if no player info is
 * available (i.e. @ref sb_has_player_info returned false).
 */
SCORBIT_SDK_EXPORT
const char *sb_get_player_picture_url(sb_game_handle_t handle, sb_player_t player);

/**
 * @brief Retrieves the player's profile picture, downloaded into an internal buffer.
 *
 * Returns the player's profile picture in binary JPEG format. The picture is only available if
 * it was configured to be downloaded during the game state setup.
 *
 * @warning Copy the returned string to your own buffer. The returned pointer remains valid only
 * until @ref sb_is_players_info_updated is called again.
 *
 * @param handle A game handle created using @ref sb_create_game_state.
 * @param player The player number (starting from 1).
 * @param size A pointer to a size_t variable that will receive the size of the buffer.
 * @return A pointer to the player's profile picture buffer, or NULL if the picture is unavailable.
 */
SCORBIT_SDK_EXPORT
const uint8_t *sb_get_player_picture(sb_game_handle_t handle, sb_player_t player, size_t *size);

#ifdef __cplusplus
}
#endif
