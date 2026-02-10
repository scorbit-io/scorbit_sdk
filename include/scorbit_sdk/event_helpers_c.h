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
#include <scorbit_sdk/export.h>
#include <stdbool.h>

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

// ------------------ OEM providers can ignore the event helpers below ------------------

SCORBIT_SDK_EXPORT
bool sb_event_config_received(const sb_event_t *event, const char **config_json);

SCORBIT_SDK_EXPORT
bool sb_event_scorbitd_update_received(const sb_event_t *event, const char **update_json);

SCORBIT_SDK_EXPORT
bool sb_event_scorbitd_updated(const sb_event_t *event, const char **version,
                               const char **executable_path);

#ifdef __cplusplus
}
#endif
