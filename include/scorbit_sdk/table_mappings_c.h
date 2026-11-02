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

#include <scorbit_sdk/export.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief How the last read of the VPIN table mappings ended. Append-only.
 */
typedef enum sb_table_mappings_result {
    SB_TABLE_MAPPINGS_NONE = 0,            ///< Not fetched yet; the list is empty.
    SB_TABLE_MAPPINGS_OK = 1,              ///< The list is the server's current one.
    SB_TABLE_MAPPINGS_NOT_VPIN_DEVICE = 2, ///< The device is not a VPIN device; the list is empty.
    SB_TABLE_MAPPINGS_NO_OWNER = 3,        ///< The device has no owner; the list is empty.
    SB_TABLE_MAPPINGS_UNAVAILABLE = 4,     ///< Network, server or reply error; last good list kept.
} sb_table_mappings_result_t;

/**
 * @brief A copy of the VPIN table mappings, owned by the caller.
 *
 * Get one with @ref sb_get_table_mappings and release it with @ref sb_table_mappings_free. Every
 * string an accessor returns is owned by the copy and stays valid until it is freed.
 */
typedef struct sb_table_mappings_struct *sb_table_mappings_t;

/// Releases @p mappings. NULL is ignored.
SCORBIT_SDK_EXPORT
void sb_table_mappings_free(sb_table_mappings_t mappings);

/// How the read behind @p mappings ended.
SCORBIT_SDK_EXPORT
sb_table_mappings_result_t sb_table_mappings_result(sb_table_mappings_t mappings);

/// The server's ETag for the list, or "" if none.
SCORBIT_SDK_EXPORT
const char *sb_table_mappings_etag(sb_table_mappings_t mappings);

/// The server's time when it built the list (ISO-8601), or "" if none.
SCORBIT_SDK_EXPORT
const char *sb_table_mappings_server_time(sb_table_mappings_t mappings);

/// The number of mappings, in the server's order.
SCORBIT_SDK_EXPORT
size_t sb_table_mappings_count(sb_table_mappings_t mappings);

/// The key type of mapping @p index (opaque to the SDK), or "" if out of range.
SCORBIT_SDK_EXPORT
const char *sb_table_mapping_key_type(sb_table_mappings_t mappings, size_t index);

/// The table key of mapping @p index (opaque to the SDK), or "" if out of range.
SCORBIT_SDK_EXPORT
const char *sb_table_mapping_table_key(sb_table_mappings_t mappings, size_t index);

/// The VenueMachine uuid mapping @p index points to, or "" if out of range.
SCORBIT_SDK_EXPORT
const char *sb_table_mapping_venuemachine(sb_table_mappings_t mappings, size_t index);

/**
 * @brief How a @ref sb_switch_machine call ended. Append-only.
 */
typedef enum sb_switch_machine_result {
    SB_SWITCH_MACHINE_OK = 0,                ///< Switched; the new machine context is in.
    SB_SWITCH_MACHINE_ALREADY_ATTACHED = 1,  ///< Already on the target; nothing changed.
    SB_SWITCH_MACHINE_GAME_ACTIVE = 2,       ///< Refused: a game is active; finish it first.
    SB_SWITCH_MACHINE_BUSY = 3,              ///< Refused: another switch is in flight.
    SB_SWITCH_MACHINE_NOT_AUTHENTICATED = 4, ///< Refused: no session with the server yet.
    SB_SWITCH_MACHINE_NOT_VPIN_DEVICE = 5,   ///< The device is not a VPIN device.
    SB_SWITCH_MACHINE_NO_OWNER = 6,          ///< The device has no owner.
    SB_SWITCH_MACHINE_TARGET_NOT_FOUND = 7,  ///< The target machine does not exist.
    SB_SWITCH_MACHINE_TARGET_NOT_OWNED = 8,  ///< The device's owner does not own the target.
    SB_SWITCH_MACHINE_TARGET_NOT_VPIN = 9,   ///< The target is not a VPIN machine.
    SB_SWITCH_MACHINE_NETWORK = 10,          ///< No response, timeout or server error; unchanged.
    SB_SWITCH_MACHINE_FAILED = 11,           ///< Anything else (malformed request or reply, ...).
} sb_switch_machine_result_t;

/**
 * @brief Receives the outcome of @ref sb_switch_machine.
 *
 * Runs at most once, on an SDK thread, never on the caller's thread and never re-entrantly; not
 * at all if the game state is destroyed first. It must not block. @p venuemachine is the target as
 * passed, valid only during the call.
 */
typedef void (*sb_switch_machine_callback_t)(sb_switch_machine_result_t result,
                                             const char *venuemachine, void *user_data);

#ifdef __cplusplus
}
#endif
