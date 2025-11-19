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

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// IMPORTANT: always add new event types at the end of the list and sync with scorbit::EventType
typedef enum {

    /**
     * @brief A game start has been requested from the mobile app.
     * Use @ref sb_event_game_start_requested to process this event.
     */
    SB_EVT_GAME_START_REQUESTED,

    /**
     * @brief A request to add credits has been received from the mobile app.
     * Use @ref sb_event_credits_add_requested to process this event.
     */
    SB_EVT_CREDITS_ADD_REQUESTED,

    /**
     * @brief A request to get the number of credits has been received from the mobile app.
     * There is no processing function for this event, the game should send current credits number
     * using @ref sb_send_credits_number.
     */
    SB_EVT_CREDITS_NUMBER_REQUESTED,

    // ------------------ OEM providers can ignore the events below ------------------

    SB_EVT_NONE = 1000, // This event shoud not be used

    SB_EVT_CONFIG_RECEIVED,

    SB_EVT_SCORBITD_UPDATE_RECEIVED,

} sb_event_type_t;

typedef struct sb_event_t sb_event_t;

/**
 * @brief Event callback function type.
 *
 * This callback function is invoked when an event occurs. The function receives a pointer to an
 * sb_event_t structure containing the event data, and a user_data pointer that was provided when
 * setting the callback using @ref sb_set_event_callback.
 *
 * - **event**: A pointer to an sb_event_t structure containing the event data.
 * - **user_data**: The user data pointer provided when setting the callback by @ref
 * sb_set_event_callback. It can be used to pass context or NULL if not needed.
 *
 * @note The event data is valid only during the callback execution. Use specific event helper
 * functions to parse data (e.g. @ref sb_event_game_start_requested, etc).
 */
typedef void (*sb_event_callback_t)(const sb_event_t *event, void *user_data);

#ifdef __cplusplus
}
#endif
