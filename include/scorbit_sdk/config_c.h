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
#include "event_types_c.h"
#include "net_types_c.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create a new configuration object.
 *
 * Allocates and initializes a new configuration object with default values.
 * The returned object must be destroyed with @ref sb_config_destroy when no longer needed.
 *
 * @return A new configuration handle, or NULL if allocation fails.
 */
SCORBIT_SDK_EXPORT
sb_config_t sb_config_create(void);

/**
 * @brief Destroy a configuration object.
 *
 * Frees all resources associated with the configuration object.
 * After calling this function, the handle is invalid and must not be used.
 *
 * @param config The configuration handle to destroy. If NULL, the function does nothing.
 */
SCORBIT_SDK_EXPORT
void sb_config_destroy(sb_config_t config);

/**
 * @brief Set the provider name.
 *
 * @param config The configuration handle.
 * @param provider The provider name, e.g., "scorbitron", "vpin". This is mandatory.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_provider(sb_config_t config, const char *provider);

/**
 * @brief Set the machine ID.
 *
 * @param config The configuration handle.
 * @param machine_id The Machine ID assigned by Scorbit, e.g., 4419. Mandatory for manufacturers.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_machine_id(sb_config_t config, int32_t machine_id);

/**
 * @brief Set the game code version.
 *
 * @param config The configuration handle.
 * @param version The game code version string, e.g., "1.12.3". This is mandatory.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_game_code_version(sb_config_t config, const char *version);

/**
 * @brief Set the server hostname.
 *
 * @param config The configuration handle.
 * @param hostname The hostname of the server to connect to. Optional.
 *                 If not set, the default "production" hostname will be used.
 *                 Examples: "production", "staging", "https://api.scorbit.io"
 */
SCORBIT_SDK_EXPORT
void sb_config_set_hostname(sb_config_t config, const char *hostname);

/**
 * @brief Set the Centrifugo server hostname.
 *
 * @param config The configuration handle.
 * @param hostname The hostname of the Centrifugo server. Optional.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_cf_hostname(sb_config_t config, const char *cf_hostname);

/**
 * @brief Set the heartbeat server hostname.
 *
 * @param config The configuration handle.
 * @param heartbeat_host The hostname of the heartbeat server. Optional.
 *                       If not set, the default "heartbeat.scorbit.io" will be used.
 *                       Intended for testing against a staging or local heartbeat server.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_heartbeat_host(sb_config_t config, const char *heartbeat_host);

/**
 * @brief Set the heartbeat server UDP port.
 *
 * @param config The configuration handle.
 * @param heartbeat_port The UDP port of the heartbeat server. Optional, set to 0 to use the
 *                       default port 8443.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_heartbeat_port(sb_config_t config, uint16_t heartbeat_port);

/**
 * @brief Set the device UUID.
 *
 * @param config The configuration handle.
 * @param uuid The device's UUID. Optional.
 *             If not set, the UUID will be derived from the device's MAC address.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_uuid(sb_config_t config, const char *uuid);

/**
 * @brief Set the device serial number.
 *
 * @param config The configuration handle.
 * @param serial_number The serial number of the device. Optional, set to 0 if unavailable.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_serial_number(sb_config_t config, uint64_t serial_number);

/**
 * @brief Enable or disable automatic player picture downloads.
 *
 * @param config The configuration handle.
 * @param enable If true, the SDK will automatically download players' profile pictures.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_auto_download_player_pics(sb_config_t config, bool enable);

/**
 * @brief Set scheduling priority for SDK-owned background threads (worker and C API queue).
 *
 * On Linux this is the value passed to setpriority(2) for each of those threads (higher means
 * lower CPU priority relative to the process baseline), and a non-zero value additionally puts
 * those threads on the SCHED_BATCH policy so the scheduler stops treating them as interactive.
 * The default is 0, which leaves thread scheduling unchanged (legacy behavior). Set a positive
 * value (for example 10) to deprioritize SDK work versus latency-sensitive threads such as audio
 * callbacks or a tight game loop on a single-core board.
 *
 * On macOS, any non-zero value requests QOS_CLASS_BACKGROUND for those threads; 0 leaves them
 * unchanged.
 *
 * @param config The configuration handle.
 * @param priority Nice value on Linux; 0 disables adjustment. Must be set before @ref
 * sb_create_game_state.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_threads_priority(sb_config_t config, int priority);

/**
 * @brief Set how many threads the SDK uses for blocking work.
 *
 * These threads run the work that waits: HTTP requests, cryptography, firmware downloads and
 * archive extraction. They do not run timers or the realtime connection, which have their own
 * threads and are unaffected by this setting.
 *
 * Optional, defaults to 4. Values outside 1-8 are clamped. On a single-core board 2 is a better
 * choice: it keeps one request able to make progress while another waits, without several threads
 * competing with the host application for the only CPU.
 *
 * @param config The configuration handle.
 * @param count Number of threads, clamped to 1-8. Must be set before @ref sb_create_game_state.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_worker_thread_count(sb_config_t config, int count);

/**
 * @brief Set score features.
 *
 * Score features help identify what triggered a score increase (e.g., ramp, spinner, target).
 *
 * @param config The configuration handle.
 * @param features An array of feature strings. Can be NULL if no features are provided.
 * @param count The number of feature strings in the array.
 * @param version The version number for the score features. Set to 1 initially,
 *                increment when new features are added in future releases.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_score_features(sb_config_t config, const char **features, size_t count,
                                  int version);

// ------------------------------------------------------------------------------------------------
// Authentication configuration - one of these must be set before creating game state
// ------------------------------------------------------------------------------------------------

/**
 * @brief Set the encrypted key for authentication.
 *
 * Use this for machines that do not have TPM. The encrypted key is generated using
 * the encrypt_tool provided with the SDK.
 *
 * @note Either this or @ref sb_config_set_signer must be called before creating the game state.
 *
 * @param config The configuration handle.
 * @param encrypted_key The encrypted private key string.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_encrypted_key(sb_config_t config, const char *encrypted_key);

/**
 * @brief Set the signer callback for TPM-based authentication.
 *
 * Use this for machines that have TPM for signing.
 *
 * @note Either this or @ref sb_config_set_encrypted_key must be called before creating
 * the game state.
 *
 * @param config The configuration handle.
 * @param signer The callback function to sign digests.
 * @param user_data User data passed to the signer callback.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_signer(sb_config_t config, sb_signer_callback_t signer, void *user_data);

// ------------------------------------------------------------------------------------------------
// Internal use only - for scorbitd
// ------------------------------------------------------------------------------------------------

SCORBIT_SDK_EXPORT
void sb_config_set_scorbitd_version(sb_config_t config, const char *version);

SCORBIT_SDK_EXPORT
void sb_config_set_scorbitd_platform_id(sb_config_t config, const char *platform_id);

SCORBIT_SDK_EXPORT
void sb_config_set_machine_title(sb_config_t config, const char *title);

SCORBIT_SDK_EXPORT
void sb_config_set_extra_fingerprint(sb_config_t config, const char *extra);

// ------------------------------------------------------------------------------------------------
// Event callback configuration
// ------------------------------------------------------------------------------------------------

/**
 * @brief Set the event callback for handling incoming events.
 *
 * Events include game start requests, credit addition requests, and other
 * notifications from the Scorbit backend.
 *
 * @param config The configuration handle.
 * @param callback The callback function to handle events. If NULL, events are ignored.
 * @param user_data Optional user data passed to the callback.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_event_callback(sb_config_t config, sb_event_callback_t callback,
                                  void *user_data);

// ------------------------------------------------------------------------------------------------
// Key persistence callbacks
// ------------------------------------------------------------------------------------------------

/**
 * @brief Set the callback for saving a key to persistent storage.
 *
 * The SDK will call this callback when it needs to persist a key. The implementation
 * should save the key to a secure persistent storage location.
 *
 * @param config The configuration handle.
 * @param callback The callback function to save the key. If NULL, key saving is disabled.
 * @param user_data Optional user data passed to the callback.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_save_key_callback(sb_config_t config, sb_save_key_callback_t callback,
                                     void *user_data);

/**
 * @brief Set the callback for loading a key from persistent storage.
 *
 * The SDK will call this callback when it needs to load a previously saved key.
 * The implementation should read the key from persistent storage and copy it to the buffer.
 * If loaded successfully, the function should return the length of the key.
 * If no key is stored, it should return 0. If the buffer is too small, it should return -1.
 *
 * @param config The configuration handle.
 * @param callback The callback function to load the key. If NULL, key loading is disabled.
 * @param user_data Optional user data passed to the callback.
 */
SCORBIT_SDK_EXPORT
void sb_config_set_load_key_callback(sb_config_t config, sb_load_key_callback_t callback,
                                     void *user_data);

#ifdef __cplusplus
}
#endif
