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

#include "scorbit_sdk/event_types_c.h"
#include "scorbit_sdk/common_types_c.h"
#include <scorbit_sdk/export.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Retrieves the type of an event.
 *
 * This function returns the type of the given event.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @return The type of the event as an sb_event_type_t value.
 */
SCORBIT_SDK_EXPORT
sb_event_type_t sb_event_type(const sb_event_t *event);

/**
 * @brief Helper function to process a game start requested event.
 *
 * This function processes an event representing a game start request.
 * The event type must be @ref SB_EVT_GAME_START_REQUESTED, otherwise the function
 * returns an error.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [OUT] players_count A pointer to an integer that will receive the number of players.
 * @return Returns true on success, or false if an error occurs (e.g., wrong event type was given).
 */
SCORBIT_SDK_EXPORT
bool sb_event_game_start_requested(const sb_event_t *event, int *players_count);

/**
 * @brief Helper function to process a credits add requested event.
 *
 * This function processes an event representing a credits add request.
 * The event type must be @ref SB_EVT_CREDITS_ADD_REQUESTED, otherwise the function
 * returns an error.
 *
 * @note After credits are added, @ref sb_set_credits_dropped should be called to notify the system.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [OUT] credits A pointer to an integer that will receive the number of credits to
 * add.
 * @param [OUT] transaction A pointer to a string that will receive the transaction ID.
 * @return Returns true on success, or false if an error occurs (e.g., wrong event type was given).
 */
SCORBIT_SDK_EXPORT
bool sb_event_credits_add_requested(const sb_event_t *event, int *credits,
                                    const char **transaction);

/**
 * @brief Helper function to extract payments enabled status from a @ref SB_EVT_CONFIG_RECEIVED
 * event.
 *
 * This function extracts the payments_enabled field from a config received event.
 * The event type must be @ref SB_EVT_CONFIG_RECEIVED, otherwise the function returns an error.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [OUT] payments_enabled A pointer to a bool that will receive the payments enabled value.
 * @return Returns true on success, or false if an error occurs (e.g., wrong event type was given).
 */
SCORBIT_SDK_EXPORT
bool sb_event_config_payments_enabled(const sb_event_t *event, bool *payments_enabled);

/**
 * @brief Helper function to process a players updated event.
 *
 * Retrieves the total number of player slots in the event. Each slot corresponds to a player
 * number from 1 to count (inclusive). Use @ref sb_event_player_has_info to determine whether
 * a slot has been claimed (profile info available) or is unclaimed (claim deeplink available).
 *
 * The event type must be @ref SB_EVT_PLAYERS_UPDATED, otherwise the function returns false.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [OUT] count A pointer to an integer that will receive the number of player slots.
 * @return Returns true on success, or false if an error occurs (e.g., wrong event type was given).
 */
SCORBIT_SDK_EXPORT
bool sb_event_players_updated(const sb_event_t *event, int *count);

/**
 * @brief Checks whether a player slot has profile info (i.e., the slot has been claimed).
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [IN] player The player number (1-based).
 * @param [OUT] has_info A pointer to a bool that will receive true if claimed, false if unclaimed.
 * @return Returns true on success, or false if an error occurs.
 */
SCORBIT_SDK_EXPORT
bool sb_event_player_has_info(const sb_event_t *event, sb_player_t player, bool *has_info);

/**
 * @brief Retrieves the player's ID for the given player number from a players updated event.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [IN] player The player number (1-based).
 * @param [OUT] id A pointer to a string pointer that will receive the player's ID.
 * @return Returns true on success, or false if an error occurs.
 */
SCORBIT_SDK_EXPORT
bool sb_event_player_id(const sb_event_t *event, sb_player_t player, const char **id);

/**
 * @brief Retrieves the player's preferred display name for the given player number.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [IN] player The player number (1-based).
 * @param [OUT] name A pointer to a string pointer that will receive the preferred name.
 * @return Returns true on success, or false if an error occurs.
 */
SCORBIT_SDK_EXPORT
bool sb_event_player_preferred_name(const sb_event_t *event, sb_player_t player, const char **name);

/**
 * @brief Retrieves the player's name for the given player number.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [IN] player The player number (1-based).
 * @param [OUT] name A pointer to a string pointer that will receive the player's name.
 * @return Returns true on success, or false if an error occurs.
 */
SCORBIT_SDK_EXPORT
bool sb_event_player_name(const sb_event_t *event, sb_player_t player, const char **name);

/**
 * @brief Retrieves the player's initials for the given player number.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [IN] player The player number (1-based).
 * @param [OUT] initials A pointer to a string pointer that will receive the initials.
 * @return Returns true on success, or false if an error occurs.
 */
SCORBIT_SDK_EXPORT
bool sb_event_player_initials(const sb_event_t *event, sb_player_t player, const char **initials);

/**
 * @brief Retrieves the player's profile picture URL for the given player number.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [IN] player The player number (1-based).
 * @param [OUT] url A pointer to a string pointer that will receive the picture URL.
 * @return Returns true on success, or false if an error occurs.
 */
SCORBIT_SDK_EXPORT
bool sb_event_player_picture_url(const sb_event_t *event, sb_player_t player, const char **url);

/**
 * @brief Retrieves the claim deeplink URL for an unclaimed player slot.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [IN] player The player number (1-based).
 * @param [OUT] url A pointer to a string pointer that will receive the claim deeplink URL.
 * @return Returns true on success, or false if an error occurs.
 */
SCORBIT_SDK_EXPORT
bool sb_event_player_claim_deeplink(const sb_event_t *event, sb_player_t player, const char **url);

/**
 * @brief Helper function to process a player picture ready event.
 *
 * Retrieves the player number and the downloaded picture data from the event.
 * The event type must be @ref SB_EVT_PLAYER_PICTURE_READY, otherwise the function returns false.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [OUT] player A pointer that will receive the player number.
 * @param [OUT] data A pointer to a uint8_t pointer that will receive the picture data.
 * @param [OUT] size A pointer to a size_t that will receive the picture data size.
 * @return Returns true on success, or false if an error occurs.
 */
SCORBIT_SDK_EXPORT
bool sb_event_player_picture_ready(const sb_event_t *event, sb_player_t *player,
                                   const uint8_t **data, size_t *size);

/**
 * @brief Helper function to process a diagnostics upload requested event.
 *
 * The event type must be @ref SB_EVT_DIAGNOSTICS_UPLOAD_REQUESTED, otherwise the function
 * returns false.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [OUT] include_recordings A pointer to a bool that will receive whether recordings
 * should be included.
 * @return Returns true on success, or false if an error occurs (e.g., wrong event type was given).
 */
SCORBIT_SDK_EXPORT
bool sb_event_diagnostics_upload_requested(const sb_event_t *event, bool *include_recordings);

/**
 * @brief Helper function to process a diagnostics uploaded event.
 *
 * The event type must be @ref SB_EVT_DIAGNOSTICS_UPLOADED, otherwise the function returns false.
 *
 * @param [IN] event A pointer to an sb_event_t structure containing the event data.
 * @param [OUT] success A pointer to a bool that will receive whether the upload succeeded.
 * @return Returns true on success, or false if an error occurs (e.g., wrong event type was given).
 */
SCORBIT_SDK_EXPORT
bool sb_event_diagnostics_uploaded(const sb_event_t *event, bool *success);

// ------------------ OEM providers can ignore the event helpers below ------------------

SCORBIT_SDK_EXPORT
bool sb_event_config_received(const sb_event_t *event, const char **config_json);

SCORBIT_SDK_EXPORT
bool sb_event_scorbitd_update_received(const sb_event_t *event, const char **update_json);

SCORBIT_SDK_EXPORT
bool sb_event_scorbitd_updated(const sb_event_t *event, const char **version,
                               const char **executable_path);

SCORBIT_SDK_EXPORT
bool sb_event_firmwares_list_received(const sb_event_t *event, const char **firmwares_list);

#ifdef __cplusplus
}
#endif
