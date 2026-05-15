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
#include "config_c.h"
#include "leaderboard_c.h"
#include "net_types_c.h"
#include "event_types_c.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a game state.
 *
 * Creates and initializes a game state using the provided configuration object.
 * Use this function at the start of the application. When the application terminates,
 * call @ref sb_destroy_game_state to release the allocated resources.
 *
 * Before calling this function, you must:
 * 1. Create a config with @ref sb_config_create
 * 2. Set required properties (provider, machine_id, game_code_version)
 * 3. Set authentication: either @ref sb_config_set_encrypted_key (for non-TPM machines)
 *    or @ref sb_config_set_signer (for TPM machines)
 *
 * @note Normally, one game state per application is sufficient.
 *
 * @param config The configuration object created by @ref sb_config_create.
 *               Must have authentication configured (encrypted key or signer).
 *
 * @return A handle to the game state, or NULL if creation fails or config is invalid.
 *
 * Example:
 * @code
 * sb_config_t config = sb_config_create();
 * sb_config_set_provider(config, "myprovider");
 * sb_config_set_machine_id(config, 4419);
 * sb_config_set_game_code_version(config, "1.0.0");
 * sb_config_set_encrypted_key(config, encrypted_key);
 *
 * sb_game_handle_t handle = sb_create_game_state(config);
 * sb_config_destroy(config);
 * @endcode
 */
SCORBIT_SDK_EXPORT
sb_game_handle_t sb_create_game_state(sb_config_t config);

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
 * @param origin The origin of the game start. This indicates how the game was started, such as
 * by pressing the start button or via a request from the lobby. See
 * @ref sb_game_start_origin_t for details and @ref SB_EVENT_GAME_START_REQUESTED
 * event.
 */
SCORBIT_SDK_EXPORT
void sb_set_game_started(sb_game_handle_t handle, sb_game_start_origin_t origin);

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
 * @brief Add a mode that expires automatically after a duration.
 *
 * Adds a mode to the game's active mode list. The SDK removes it when the duration elapses; you do
 * not need to call @ref sb_remove_mode for that (though you may still call it to remove the mode
 * early). If the same mode is added again before it expires, it is moved to the front of the mode
 * list and the expiration time is reset using the new duration.
 *
 * @param duration_seconds Unsigned duration in whole seconds. **0** is normalized to **3** seconds
 * (recommended default for transient UI modes). Values **greater than 10** are clamped to **10**
 * seconds (maximum). Values **1–10** are used as-is. **3** seconds is the recommended duration.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 * @param mode The mode to add (e.g., "MB:Multiball").
 * @param duration_seconds See normalization rules above.
 *
 * Example (recommended 3 second duration):
 * @code
 * sb_add_mode_expiring(handle, "MB:Multiball", 3);
 * @endcode
 */
SCORBIT_SDK_EXPORT
void sb_add_mode_expiring(sb_game_handle_t handle, const char *mode, uint32_t duration_seconds);

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
 * sb_add_mode, @ref sb_add_mode_expiring, @ref sb_remove_mode, or @ref sb_clear_modes.
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
 * @brief Retrieve the machine serial number.
 *
 * The value matches @ref sb_device_info_t::serial_number (TPM / provisioning): a 64-bit unsigned
 * integer. It comes from the device configuration (@ref sb_config_set_serial_number) and may be
 * updated after key resolution / provisioning when applicable.
 *
 * @param handle The game handle created using @ref sb_create_game_state.
 * @return The serial number, or ``0`` if unset.
 */
SCORBIT_SDK_EXPORT
uint64_t sb_get_machine_serial(sb_game_handle_t handle);

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
 * @brief Retrieves the top scores from the leaderboard.
 *
 * @note The callback function is invoked asynchronously when the operation completes, running in
 * a separate thread from the main calling thread. It is recommended to use appropriate locks (e.g.,
 * a mutex) when accessing shared data.
 *
 * @param handle The game handle created using @ref sb_create_game_state.
 * @param scope Selects whether the request targets the paired venue machine leaderboard, the
 * variant-wide leaderboard for the paired title, or the shared game leaderboard.
 * @param period Selects the backend time bucket. Use @ref SB_LEADERBOARD_PERIOD_ALL_TIME for the
 * unfiltered all-time leaderboard.
 * @param since Optional UTC ISO-8601 lower-bound time filter. When set, only scores created at or
 * after this timestamp are returned and the @p period parameter is ignored by the backend.
 * Pass NULL to omit this filter.
 * @param vpin_filter Controls whether virtual pinball scores are included. Use
 * @ref SB_LEADERBOARD_VPIN_ANY to include both virtual and physical scores.
 * @param callback A callback function of @ref sb_leaderboard_callback_t. On success,
 * @p leaderboard is non-NULL and valid only during the callback; do not store it or free it.
 * Otherwise, @p leaderboard is NULL. Error codes include @ref SB_EC_NOT_PAIRED,
 * @ref SB_EC_AUTH_FAILED, and @ref SB_EC_API_ERROR.
 *
 * If pairing or machine context required for @p scope is not available yet, the SDK defers the
 * HTTP request and retries automatically until the context is ready or a terminal error occurs.
 * The callback is invoked only when the request completes or fails terminally.
 * @param user_data Optional user data to pass to the callback. Pass NULL if not used.
 */
SCORBIT_SDK_EXPORT
void sb_request_top_scores(sb_game_handle_t handle, sb_leaderboard_scope_t scope,
                           sb_leaderboard_period_t period, const char *since,
                           sb_leaderboard_vpin_filter_t vpin_filter,
                           sb_leaderboard_callback_t callback, void *user_data);

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

// -------------------------- SETTINGS ----------------------------------

/**
 * @brief Sets the device capabilities.
 *
 * Configures the device with the features it supports. The @p capabilities
 * argument should contain a bitwise OR of one or more values of @ref sb_capability_t.
 *
 * @note If this function is not called, all capabilities are assumed to be disabled by default.
 *
 * @param capabilities Bitwise OR of capability flags supported by the device.
 */
SCORBIT_SDK_EXPORT
void sb_set_capabilities(sb_game_handle_t handle, sb_capabilities_t capabilities);

// -------------------------- CREDITS / STATUS ----------------------------------

/**
 * @brief Sets the number of credits dropped into the machine.
 *
 * This function should be called when @ref SB_EVT_CREDITS_ADD_REQUESTED event received and credits
 * added to machine. It notifies the Scorbit cloud service and mobile app dropped credits count and
 * if it was successful.
 *
 * @note it should not be called if physical coins dropped in to machine.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 * @param credits The number of credits dropped into the machine.
 * @param transaction The transaction ID associated with the credit drop (passed in the event).
 * @param success true if the credit drop was successful; false otherwise.
 */
SCORBIT_SDK_EXPORT
void sb_set_credits_dropped(sb_game_handle_t handle, int credits, const char *transaction,
                            bool success);

/**
 * @brief Sets the current credits status.
 *
 * This function should be called:
 * 1. when @ref SB_EVT_CREDITS_STATUS_REQUESTED event received
 * 2. when credits number changed in machine (added or subtracted)
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 * @param free_play true if the machine is in free play mode; false otherwise.
 * @param credits The current number of credits available in the machine.
 * @param max_credits The maximum number of credits allowed in the machine.
 * @param pricing For future use. Currently should be set to NULL or an empty string.
 */
SCORBIT_SDK_EXPORT
void sb_set_credits_status(sb_game_handle_t handle, bool free_play, int credits, int max_credits,
                           const char *pricing);

/**
 * @brief Upload diagnostics (logs, recordings, and arbitrary text) to the Scorbit API.
 *
 * Gathers the provided files and text, creates a tar.gz archive, and uploads it.
 * The SDK enforces size limits: max 5 log files (each <= 10 MB), max 2 recording files
 * (each <= 20 MB), and log string truncated to 10 MB. Files exceeding limits are skipped.
 *
 * When the upload completes, a @ref SB_EVT_DIAGNOSTICS_UPLOADED event is fired.
 *
 * @param handle The game handle created by @ref sb_create_game_state.
 * @param log_paths Array of file paths to log files. Pass NULL if no log files.
 * @param log_count Number of entries in log_paths.
 * @param recording_paths Array of file paths to recording files. Pass NULL if no recordings.
 * @param recording_count Number of entries in recording_paths.
 * @param log_string Arbitrary log text to include. Pass NULL or empty if not needed.
 */
SCORBIT_SDK_EXPORT
void sb_upload_diagnostics(sb_game_handle_t handle, const char **log_paths, size_t log_count,
                           const char **recording_paths, size_t recording_count,
                           const char *log_string);

// -------------------------- INTERNAL FOR SCORBIT  --------------------------------------

SCORBIT_SDK_EXPORT
void sb_game_request_pair_machine(sb_game_handle_t handle, const char *machine_uuid,
                                  const char *owner_uuid, sb_string_callback_t callback,
                                  void *user_data);

/**
 * @brief Download a file from a URL and save it to local storage.
 *
 * @note The callback function is invoked asynchronously when the operation completes, running in
 * a separate thread from the main calling thread.
 *
 * @param handle The game handle created using @ref sb_create_game_state.
 * @param url The URL to download from.
 * @param filename The local filename to save the downloaded file to.
 * @param headers Optional array of HTTP headers to include in the request. Pass NULL if not used.
 * @param headers_count Number of elements in the @p headers array. Pass 0 if not used.
 * @param callback A callback function of @ref sb_string_callback_t that receives the result.
 * Returns @ref SB_EC_SUCCESS if the download was successful. On success, the reply string contains
 * the path to the downloaded file. Otherwise, it returns an error code.
 * @param user_data Optional user data to pass to the callback. Pass NULL if not used.
 */
SCORBIT_SDK_EXPORT
void sb_download(sb_game_handle_t handle, const char *url, const char *filename,
                 const sb_http_header_t *headers, size_t headers_count,
                 sb_string_callback_t callback, void *user_data);

/**
 * @brief Download data from a URL into a memory buffer.
 *
 * @note The callback function is invoked asynchronously when the operation completes, running in
 * a separate thread from the main calling thread.
 *
 * @param handle The game handle created using @ref sb_create_game_state.
 * @param url The URL to download from.
 * @param reserve_buffer_size The initial buffer size to reserve for the download.
 * @param headers Optional array of HTTP headers to include in the request. Pass NULL if not used.
 * @param headers_count Number of elements in the @p headers array. Pass 0 if not used.
 * @param callback A callback function of @ref sb_buffer_callback_t that receives the downloaded
 * data. Returns @ref SB_EC_SUCCESS if the download was successful. The data pointer is valid only
 * during the callback execution.
 * @param user_data Optional user data to pass to the callback. Pass NULL if not used.
 */
SCORBIT_SDK_EXPORT
void sb_download_buffer(sb_game_handle_t handle, const char *url, size_t reserve_buffer_size,
                        const sb_http_header_t *headers, size_t headers_count,
                        sb_buffer_callback_t callback, void *user_data);

#ifdef __cplusplus
}
#endif
