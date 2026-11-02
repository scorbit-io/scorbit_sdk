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

#include <scorbit_sdk/scorbit_sdk_c.h>

#include <godot_cpp/variant/dictionary.hpp>

// The add-on loads the SDK itself instead of linking it, so it can load the copy in the game's
// user folder, which the SDK's updater can replace, and fall back to the copy shipped beside it.

// Every SDK function the add-on calls. A call goes through the table: api().sb_name(...).
#define SCORBIT_GODOT_SDK_FUNCTIONS(X)                                                             \
    X(sb_add_logger_callback)                                                                      \
    X(sb_add_mode)                                                                                 \
    X(sb_add_mode_expiring)                                                                        \
    X(sb_clear_modes)                                                                              \
    X(sb_commit)                                                                                   \
    X(sb_config_create)                                                                            \
    X(sb_config_destroy)                                                                           \
    X(sb_config_set_auto_download_player_pics)                                                     \
    X(sb_config_set_cf_hostname)                                                                   \
    X(sb_config_set_encrypted_key)                                                                 \
    X(sb_config_set_event_callback)                                                                \
    X(sb_config_set_extra_fingerprint)                                                             \
    X(sb_config_set_game_code_version)                                                             \
    X(sb_config_set_game_firmware)                                                                 \
    X(sb_config_set_heartbeat_host)                                                                \
    X(sb_config_set_heartbeat_port)                                                                \
    X(sb_config_set_hostname)                                                                      \
    X(sb_config_set_load_key_callback)                                                             \
    X(sb_config_set_machine_id)                                                                    \
    X(sb_config_set_machine_title)                                                                 \
    X(sb_config_set_provider)                                                                      \
    X(sb_config_set_save_key_callback)                                                             \
    X(sb_config_set_score_features)                                                                \
    X(sb_config_set_serial_number)                                                                 \
    X(sb_config_set_threads_priority)                                                              \
    X(sb_config_set_uuid)                                                                          \
    X(sb_config_set_worker_thread_count)                                                           \
    X(sb_create_game_state)                                                                        \
    X(sb_destroy_game_state)                                                                       \
    X(sb_download)                                                                                 \
    X(sb_download_buffer)                                                                          \
    X(sb_event_config_received)                                                                    \
    X(sb_event_credits_add_requested)                                                              \
    X(sb_event_diagnostics_upload_requested)                                                       \
    X(sb_event_diagnostics_uploaded)                                                               \
    X(sb_event_game_start_requested)                                                               \
    X(sb_event_pairing_status_changed)                                                             \
    X(sb_event_player_claim_deeplink)                                                              \
    X(sb_event_player_has_info)                                                                    \
    X(sb_event_player_id)                                                                          \
    X(sb_event_player_initials)                                                                    \
    X(sb_event_player_name)                                                                        \
    X(sb_event_player_picture_ready)                                                               \
    X(sb_event_player_picture_url)                                                                 \
    X(sb_event_player_preferred_name)                                                              \
    X(sb_event_players_updated)                                                                    \
    X(sb_event_pricing_bundle_credits)                                                             \
    X(sb_event_pricing_bundle_price)                                                               \
    X(sb_event_pricing_bundle_regular_price)                                                       \
    X(sb_event_pricing_bundle_sale_price)                                                          \
    X(sb_event_pricing_bundles_count)                                                              \
    X(sb_event_pricing_credit_price)                                                               \
    X(sb_event_pricing_credit_regular_price)                                                       \
    X(sb_event_pricing_credit_sale_price)                                                          \
    X(sb_event_pricing_free_play)                                                                  \
    X(sb_event_pricing_payments_enabled)                                                           \
    X(sb_event_type)                                                                               \
    X(sb_game_request_pair_machine)                                                                \
    X(sb_get_machine_serial)                                                                       \
    X(sb_get_machine_uuid)                                                                         \
    X(sb_get_pair_deeplink)                                                                        \
    X(sb_get_status)                                                                               \
    X(sb_leaderboard_entries_count)                                                                \
    X(sb_leaderboard_entry_created)                                                                \
    X(sb_leaderboard_entry_high_score)                                                             \
    X(sb_leaderboard_entry_id)                                                                     \
    X(sb_leaderboard_entry_image)                                                                  \
    X(sb_leaderboard_entry_is_nfc_verified)                                                        \
    X(sb_leaderboard_entry_is_verified)                                                            \
    X(sb_leaderboard_entry_is_vpin)                                                                \
    X(sb_leaderboard_entry_player_avatar)                                                          \
    X(sb_leaderboard_entry_player_display_name)                                                    \
    X(sb_leaderboard_entry_player_follower_count)                                                  \
    X(sb_leaderboard_entry_player_following_count)                                                 \
    X(sb_leaderboard_entry_player_id)                                                              \
    X(sb_leaderboard_entry_player_initials)                                                        \
    X(sb_leaderboard_entry_player_last_login)                                                      \
    X(sb_leaderboard_entry_player_username)                                                        \
    X(sb_leaderboard_entry_rank)                                                                   \
    X(sb_leaderboard_entry_reaction_count)                                                         \
    X(sb_leaderboard_entry_score_count)                                                            \
    X(sb_logger_callbacks_supported)                                                               \
    X(sb_remove_mode)                                                                              \
    X(sb_report_device_state)                                                                      \
    X(sb_request_pair_code)                                                                        \
    X(sb_request_top_scores)                                                                       \
    X(sb_request_unpair)                                                                           \
    X(sb_reset_logger)                                                                             \
    X(sb_set_active_player)                                                                        \
    X(sb_set_capabilities)                                                                         \
    X(sb_set_credits_dropped)                                                                      \
    X(sb_set_credits_status)                                                                       \
    X(sb_set_current_ball)                                                                         \
    X(sb_set_game_finished)                                                                        \
    X(sb_set_game_started)                                                                         \
    X(sb_set_mode_completed)                                                                       \
    X(sb_set_score)                                                                                \
    X(sb_upload_diagnostics_ex)

namespace scorbit_godot {

struct SdkApi {
#define SCORBIT_GODOT_SDK_MEMBER(name) decltype(&::name) name = nullptr;
    SCORBIT_GODOT_SDK_FUNCTIONS(SCORBIT_GODOT_SDK_MEMBER)
#undef SCORBIT_GODOT_SDK_MEMBER
};

/// Loads the SDK on first use (main thread). False if no copy loads; the reason is pushed as an
/// error. Later calls return the first result.
bool loadSdk();

/// Whether loadSdk() succeeded.
bool sdkLoaded();

/// The loaded SDK's functions. Only after loadSdk() returned true.
const SdkApi &api();

/// Where the SDK is looked for and which file was loaded: bundled, copy, sidecar, loaded.
godot::Dictionary sdkPaths();

} // namespace scorbit_godot
