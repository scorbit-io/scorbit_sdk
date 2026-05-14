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
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SB_DIGEST_LENGTH = 32,
    SB_UUID_LENGTH = 16,
    SB_SIGNATURE_MAX_LENGTH = 72, // ECDSA signature length for P-256
    SB_KEY_LENGTH = 32,           // ECDSA key length for P-256
};

// IMPORTANT: always add new error codes at the end of the list and sync with scorbit::Error
typedef enum {
    SB_EC_SUCCESS = 0,     // Success
    SB_EC_UNKNOWN = 1,     // Unknown error
    SB_EC_AUTH_FAILED = 2, // Authentication failed
    SB_EC_NOT_PAIRED = 3,  // Device is not paired
    SB_EC_API_ERROR = 4,   // API call error (e.g., HTTP error code != 200)
    SB_EC_FILE_ERROR = 5,  // File error (e.g., can't open file)
} sb_error_t;

typedef enum {
    SB_NET_NOT_AUTHENTICATED = 0,              // NotAuthenticated,
    SB_NET_AUTHENTICATING = 1,                 // Authenticating,
    SB_NET_AUTHENTICATED_CHECKING_PAIRING = 2, // AuthenticatedCheckingPairing,
    SB_NET_AUTHENTICATED_UNPAIRED = 3,         // AuthenticatedUnpaired,
    SB_NET_AUTHENTICATED_PAIRED = 4,           // AuthenticatedPaired,
    SB_NET_AUTHENTICATION_FAILED = 5,          // AuthenticationFailed,
} sb_auth_status_t;

typedef enum {
    SB_GAME_STARTED_BY_BUTTON = 0,  // started by the machine when player press Start button
    SB_GAME_STARTED_FROM_LOBBY = 1, // started explicitly via Scorbit app request
} sb_game_start_origin_t;

typedef enum {
    SB_LEADERBOARD_SCOPE_MACHINE = 0, // Specific paired venue machine leaderboard
    SB_LEADERBOARD_SCOPE_VARIANT = 1, // Shared machine variant leaderboard
    SB_LEADERBOARD_SCOPE_GAME = 2,    // Shared game leaderboard across variants
} sb_leaderboard_scope_t;

typedef enum {
    SB_LEADERBOARD_PERIOD_ALL_TIME = 0, // All-time leaderboard
    SB_LEADERBOARD_PERIOD_14D = 1,      // Rolling 14-day leaderboard
    SB_LEADERBOARD_PERIOD_30D = 2,      // Rolling 30-day leaderboard
    SB_LEADERBOARD_PERIOD_90D = 3,      // Rolling 90-day leaderboard
    SB_LEADERBOARD_PERIOD_180D = 4,     // Rolling 180-day leaderboard
    SB_LEADERBOARD_PERIOD_365D = 5,     // Rolling 365-day leaderboard
} sb_leaderboard_period_t;

typedef enum {
    SB_LEADERBOARD_VPIN_ANY = 0,       // Include both virtual and physical scores
    SB_LEADERBOARD_VPIN_ONLY = 1,      // Only virtual pinball scores
    SB_LEADERBOARD_VPIN_REAL_ONLY = 2, // Exclude virtual pinball scores
} sb_leaderboard_vpin_filter_t;

typedef enum {
    SB_CAPABILITY_START_GAME = 1u << 0,  // Game can be started remotely
    SB_CAPABILITY_CREDIT_DROP = 1u << 1, // Machine can accept coin drop events
} sb_capability_t;
typedef uint32_t sb_capabilities_t;

typedef struct {
    /** Mandatory. The provider name, e.g., "scorbitron", "vpin". */
    const char *provider;

    /** Mandatory for manufacturers. The Machine ID assigned by Scorbit, ex. 4419. */
    int32_t machine_id;

    /** Mandatory. The game code version, e.g., "1.12.3". */
    const char *game_code_version;

    /**
     * Optional. The hostname of the server to connect to.
     * If set to `NULL`, the default `"production"` hostname will be used. The two standard options
     * are `"production"` and `"staging"`, each mapped to pre-defined URLs. To use a custom URL,
     * provide it in the format `"http[s]://<url>[:port]"`.
     *
     * Examples: `"production"`, `"staging"`, `"https://api.scorbit.io"`,
     * `"https://staging.scorbit.io"`, `"http://localhost:8080"`
     */
    const char *hostname;

    /**
     * Optional. The device's UUID.
     * If set to `NULL`, the UUID will be derived from the device's MAC address.
     *
     * Examples: `"f0b188f8-9f2d-4f8d-abe4-c3107516e7ce"`, `"f0b188f89f2d4f8dabe4c3107516e7ce"`,
     * `"F0B188F8-9F2D-4F8D-ABE4-C3107516E7CE"`, `"F0B188F89F2D4F8DABE4C3107516E7CE"`
     */
    const char *uuid;

    /** Optional. The serial number of the device. Set to 0 if unavailable. */
    uint64_t serial_number;

    /** If true, the SDK will automatically download players' profile pictures */
    bool auto_download_player_pics;

    /**
     * Optional. An array of score features that help identify what triggered a score increase
     * (e.g., ramp, spinner, target, etc.).
     * Set it to NULL if no specific features are provided.
     *
     * Example: info.score_features = {"ramp", "left spinner", "right spinner"};
     */
    const char **score_features;

    /**
     * Optional. The number of strings of the score_features array. If don't provide score
     * features, set to 0. See @ref score_features.
     */
    size_t score_features_count;

    /**
     * Optional. Version number for the score features.
     * Set to 1 if score features are used. Increment this version when new features are added
     * in future game releases.
     * Ignored if @ref score_features is empty.
     */
    int score_features_version;

    // ------ for internal use only -------
    const char *scorbitd_version;
    const char *scorbitd_platform_id;

} sb_device_info_t;

/**
 * Signer callback function type.
 *
 * Sign digest using given key and store the result in signature, the length of signature store in
 * signature_len. Key and digest are fixed length long byte arrays. The user_data is the data passed
 * to the signer. It can be used to pass the context to the signer or NULL if not used.
 *
 * - **signature**: The buffer to store the signature.
 * - **signature_len**: The length of the signature.
 * - **digest**: The digest to sign.
 * - **user_data**: The user data passed to the signer.
 *
 * - returns 0 if the signing was successful
 */
typedef int (*sb_signer_callback_t)(uint8_t signature[SB_SIGNATURE_MAX_LENGTH],
                                    size_t *signature_len, const uint8_t digest[SB_DIGEST_LENGTH],
                                    void *user_data);

/**
 * @brief HTTP header key-value pair for use with download functions.
 */
typedef struct {
    const char *name;
    const char *value;
} sb_http_header_t;

typedef void (*sb_string_callback_t)(sb_error_t error, const char *reply, void *user_data);

typedef void (*sb_buffer_callback_t)(sb_error_t error, const uint8_t *data, size_t size,
                                     void *user_data);

/**
 * Save key callback function type.
 *
 * Called by the SDK to persist a key. The implementation should save the key
 * to persistent storage (e.g., file, database, secure storage).
 *
 * - **key**: The null-terminated key string to save.
 * - **user_data**: The user data passed when setting the callback.
 */
typedef void (*sb_save_key_callback_t)(const char *key, void *user_data);

/**
 * Load key callback function type.
 *
 * Called by the SDK to load a previously saved key. The implementation should
 * read the key from persistent storage and copy it to the provided buffer.
 *
 * - **buffer**: The buffer to store the loaded key (null-terminated).
 * - **buffer_size**: The size of the buffer in bytes (inlcuding terminal 0).
 * - **user_data**: The user data passed when setting the callback.
 *
 * - returns The actual length of the key (excluding null terminator), or -1 if
 *           buffer_size is too small. If no key is stored, return 0 and leave
 *           buffer empty.
 */
typedef int (*sb_load_key_callback_t)(char *buffer, size_t buffer_size, void *user_data);

// ------------------------------------------------------------------------------------------------
// sb_config_t - Opaque configuration handle for ABI-stable extensibility
// ------------------------------------------------------------------------------------------------

/**
 * @brief Opaque configuration handle.
 *
 * This is an opaque pointer to a configuration object. Use the sb_config_* functions to create,
 * configure, and destroy configuration objects. This design allows adding new configuration options
 * in future SDK versions without breaking ABI compatibility.
 */
typedef struct sb_config_s *sb_config_t;

#ifdef __cplusplus
}
#endif
