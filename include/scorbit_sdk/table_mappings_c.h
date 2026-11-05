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

#include <stdbool.h>
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

/**
 * @brief How a @ref sb_match_table_mapping call ended. Append-only.
 */
typedef enum sb_table_match_result {
    SB_TABLE_MATCH_MAPPED = 0,              ///< 200: the mapping and its target are filled.
    SB_TABLE_MATCH_NEEDS_MAPPING = 1,       ///< 409 venuemachine_needed: map the table first.
    SB_TABLE_MATCH_UNKNOWN_ROM = 2,         ///< 404 unknown_rom.
    SB_TABLE_MATCH_UNKNOWN_SIGNATURE = 3,   ///< 404 unknown_signature.
    /// Any 413: signature_too_large, or a proxy's body limit with no code (error_code "").
    SB_TABLE_MATCH_SIGNATURE_TOO_LARGE = 4,
    /// Any 400 (invalid_table_key, invalid_signature, signature_conflict, required, invalid,
    /// ...), or refused before sending: both kinds of evidence, or signatures JSON that is not a
    /// non-empty array.
    SB_TABLE_MATCH_INVALID = 5,
    SB_TABLE_MATCH_NOT_VPIN_DEVICE = 6,   ///< 403 not_vpin_device.
    SB_TABLE_MATCH_NO_OWNER = 7,          ///< 403 no_owner.
    SB_TABLE_MATCH_NOT_AUTHENTICATED = 8, ///< Authentication failed for good, or its wait ended.
    SB_TABLE_MATCH_NETWORK = 9,           ///< No response, timeout, 5xx or 429.
    /// Anything else: a malformed 200, an unknown 4xx, another 409, or an api without the route
    /// (404 or 405 with no code).
    SB_TABLE_MATCH_FAILED = 10,
} sb_table_match_result_t;

/**
 * @brief A table-mapping lookup request, built by the caller.
 *
 * Create one with @ref sb_table_match_request_create, fill it with the setters and pass it to
 * @ref sb_match_table_mapping, which copies it: it may be freed with
 * @ref sb_table_match_request_free any time after that call returns. Every setter ignores a NULL
 * request.
 */
typedef struct sb_table_match_request_struct *sb_table_match_request_t;

/// A new, empty request, or NULL if allocation fails.
SCORBIT_SDK_EXPORT
sb_table_match_request_t sb_table_match_request_create(void);

/// Releases @p request. NULL is ignored.
SCORBIT_SDK_EXPORT
void sb_table_match_request_free(sb_table_match_request_t request);

/**
 * @brief Sets the key to look up. Both strings are passed through as given; NULL is "".
 *
 * @param request The request.
 * @param key_type The key type, e.g. "rom", "script_id" or "signature".
 * @param table_key The table key of that type.
 */
SCORBIT_SDK_EXPORT
void sb_table_match_request_set_key(sb_table_match_request_t request, const char *key_type,
                                    const char *table_key);

/// Sets the firmware id sent with the key. NULL or "" leaves it out of the request.
SCORBIT_SDK_EXPORT
void sb_table_match_request_set_firmware_id(sb_table_match_request_t request,
                                            const char *firmware_id);

/**
 * @brief Sets the table's signatures: a JSON array, sent as the caller's text, byte for byte.
 *
 * The SDK checks only that it is a non-empty JSON array; the server checks its contents. NULL
 * clears it. Not together with @ref sb_table_match_request_set_signature_unavailable.
 */
SCORBIT_SDK_EXPORT
void sb_table_match_request_set_signatures_json(sb_table_match_request_t request, const char *json);

/**
 * @brief Sets why the table has no signatures, a code passed through as given. NULL clears it.
 *
 * Not together with @ref sb_table_match_request_set_signatures_json.
 */
SCORBIT_SDK_EXPORT
void sb_table_match_request_set_signature_unavailable(sb_table_match_request_t request,
                                                      const char *code);

/**
 * @brief The answer to a @ref sb_match_table_mapping call.
 *
 * Valid only during the callback that receives it, as are the strings its accessors return. Every
 * accessor accepts NULL. The strings are "" when absent, and the mapping fields are "" unless the
 * result is @ref SB_TABLE_MATCH_MAPPED.
 */
typedef struct sb_table_match_struct *sb_table_match_t;

/**
 * @brief Receives the outcome of @ref sb_match_table_mapping.
 *
 * Runs at most once, on an SDK thread, never on the caller's thread and never re-entrantly. Once
 * the game state is being destroyed it runs with @ref SB_TABLE_MATCH_FAILED or not at all. It must
 * not block, and may call any SDK function. @p match is valid only during the call.
 */
typedef void (*sb_table_match_callback_t)(sb_table_match_result_t result, sb_table_match_t match,
                                          void *user_data);

/// The HTTP status of the answer, 0 when there was no response.
SCORBIT_SDK_EXPORT
int sb_table_match_http_status(sb_table_match_t match);

/// On every answer but a 200: the api's errors[0].code, else its top-level code, else "".
SCORBIT_SDK_EXPORT
const char *sb_table_match_error_code(sb_table_match_t match);

/// On every answer but a 200: the api's errors[0].attr, "" when it is null or absent.
SCORBIT_SDK_EXPORT
const char *sb_table_match_error_attr(sb_table_match_t match);

/// The mapping's key type (opaque to the SDK).
SCORBIT_SDK_EXPORT
const char *sb_table_match_key_type(sb_table_match_t match);

/// The mapping's table key, as the server normalised it.
SCORBIT_SDK_EXPORT
const char *sb_table_match_table_key(sb_table_match_t match);

/// The mapping's uuid.
SCORBIT_SDK_EXPORT
const char *sb_table_match_mapping_id(sb_table_match_t match);

/// The VenueMachine uuid the mapping points to: the target for @ref sb_switch_machine.
SCORBIT_SDK_EXPORT
const char *sb_table_match_venuemachine(sb_table_match_t match);

/// The VenueMachine's name.
SCORBIT_SDK_EXPORT
const char *sb_table_match_venuemachine_name(sb_table_match_t match);

/// The uuid of the VenueMachine's Machine.
SCORBIT_SDK_EXPORT
const char *sb_table_match_machine(sb_table_match_t match);

/// The name of the VenueMachine's Machine.
SCORBIT_SDK_EXPORT
const char *sb_table_match_machine_name(sb_table_match_t match);

/// Whether the answer carries the server's signature verdict.
SCORBIT_SDK_EXPORT
bool sb_table_match_has_verdict(sb_table_match_t match);

/// The verdict's status as the server sent it (opaque to the SDK), e.g. "match" or "modified".
SCORBIT_SDK_EXPORT
const char *sb_table_match_verdict_status(sb_table_match_t match);

/// The verdict's accepted version, "" when null.
SCORBIT_SDK_EXPORT
const char *sb_table_match_verdict_accepted_version(sb_table_match_t match);

/// The number of changed sections in the verdict.
SCORBIT_SDK_EXPORT
size_t sb_table_match_verdict_changed_sections_count(sb_table_match_t match);

/// Changed section @p index of the verdict, or "" if out of range.
SCORBIT_SDK_EXPORT
const char *sb_table_match_verdict_changed_section(sb_table_match_t match, size_t index);

/// The number of changed procedures in the verdict.
SCORBIT_SDK_EXPORT
size_t sb_table_match_verdict_changed_procedures_count(sb_table_match_t match);

/// Changed procedure @p index of the verdict, or "" if out of range.
SCORBIT_SDK_EXPORT
const char *sb_table_match_verdict_changed_procedure(sb_table_match_t match, size_t index);

#ifdef __cplusplus
}
#endif
