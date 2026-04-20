# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Raw ctypes declarations for every exported C function in the Scorbit SDK.

All functions are declared on the module-level ``_lib`` object imported from
:mod:`scorbit._loader`.  Callback function-pointer types (``CFUNCTYPE``) are
also defined here so higher-level modules can create type-safe trampolines.
"""

from ctypes import (
    CFUNCTYPE,
    POINTER,
    c_bool,
    c_char,
    c_char_p,
    c_int,
    c_int32,
    c_int64,
    c_size_t,
    c_uint,
    c_uint8,
    c_uint32,
    c_uint64,
    c_void_p,
)

from ._loader import _lib

# ---------------------------------------------------------------------------
# Opaque handle types (all represented as void*)
# ---------------------------------------------------------------------------
sb_game_handle_t = c_void_p
sb_config_t = c_void_p

# ---------------------------------------------------------------------------
# Constants from net_types_c.h
# ---------------------------------------------------------------------------
SB_DIGEST_LENGTH = 32
SB_UUID_LENGTH = 16
SB_SIGNATURE_MAX_LENGTH = 72
SB_KEY_LENGTH = 32

# ---------------------------------------------------------------------------
# C callback function-pointer types
# ---------------------------------------------------------------------------

# void (*sb_event_callback_t)(const sb_event_t *event, void *user_data)
sb_event_callback_t = CFUNCTYPE(None, c_void_p, c_void_p)

# void (*sb_string_callback_t)(sb_error_t error, const char *reply, void *user_data)
sb_string_callback_t = CFUNCTYPE(None, c_int, c_char_p, c_void_p)

# void (*sb_buffer_callback_t)(sb_error_t error, const uint8_t *data, size_t size, void *user_data)
sb_buffer_callback_t = CFUNCTYPE(None, c_int, POINTER(c_uint8), c_size_t, c_void_p)

# int (*sb_signer_callback_t)(uint8_t signature[72], size_t *signature_len,
#                              const uint8_t digest[32], void *user_data)
sb_signer_callback_t = CFUNCTYPE(
    c_int,
    POINTER(c_uint8),       # signature buffer
    POINTER(c_size_t),      # signature_len out
    POINTER(c_uint8),       # digest
    c_void_p,               # user_data
)

# void (*sb_save_key_callback_t)(const char *key, void *user_data)
sb_save_key_callback_t = CFUNCTYPE(None, c_char_p, c_void_p)

# int (*sb_load_key_callback_t)(char *buffer, size_t buffer_size, void *user_data)
# POINTER(c_char): writable buffer; c_char_p becomes immutable str/bytes in callbacks.
sb_load_key_callback_t = CFUNCTYPE(c_int, POINTER(c_char), c_size_t, c_void_p)

# void (*sb_log_callback_t)(const char *message, sb_log_level_t level,
#                            const char *file, int line, int64_t timestamp, void *user_data)
sb_log_callback_t = CFUNCTYPE(
    None,
    c_char_p,   # message
    c_int,      # level
    c_char_p,   # file
    c_int,      # line
    c_int64,    # timestamp
    c_void_p,   # user_data
)

# ---------------------------------------------------------------------------
# config_c.h
# ---------------------------------------------------------------------------

# sb_config_t sb_config_create(void)
_lib.sb_config_create.restype = sb_config_t
_lib.sb_config_create.argtypes = []

# void sb_config_destroy(sb_config_t config)
_lib.sb_config_destroy.restype = None
_lib.sb_config_destroy.argtypes = [sb_config_t]

# void sb_config_set_provider(sb_config_t, const char*)
_lib.sb_config_set_provider.restype = None
_lib.sb_config_set_provider.argtypes = [sb_config_t, c_char_p]

# void sb_config_set_machine_id(sb_config_t, int32_t)
_lib.sb_config_set_machine_id.restype = None
_lib.sb_config_set_machine_id.argtypes = [sb_config_t, c_int32]

# void sb_config_set_game_code_version(sb_config_t, const char*)
_lib.sb_config_set_game_code_version.restype = None
_lib.sb_config_set_game_code_version.argtypes = [sb_config_t, c_char_p]

# void sb_config_set_hostname(sb_config_t, const char*)
_lib.sb_config_set_hostname.restype = None
_lib.sb_config_set_hostname.argtypes = [sb_config_t, c_char_p]

# void sb_config_set_cf_hostname(sb_config_t, const char*)
_lib.sb_config_set_cf_hostname.restype = None
_lib.sb_config_set_cf_hostname.argtypes = [sb_config_t, c_char_p]

# void sb_config_set_uuid(sb_config_t, const char*)
_lib.sb_config_set_uuid.restype = None
_lib.sb_config_set_uuid.argtypes = [sb_config_t, c_char_p]

# void sb_config_set_serial_number(sb_config_t, uint64_t)
_lib.sb_config_set_serial_number.restype = None
_lib.sb_config_set_serial_number.argtypes = [sb_config_t, c_uint64]

# void sb_config_set_auto_download_player_pics(sb_config_t, bool)
_lib.sb_config_set_auto_download_player_pics.restype = None
_lib.sb_config_set_auto_download_player_pics.argtypes = [sb_config_t, c_bool]

# void sb_config_set_threads_priority(sb_config_t, int)
_lib.sb_config_set_threads_priority.restype = None
_lib.sb_config_set_threads_priority.argtypes = [sb_config_t, c_int]

# void sb_config_set_score_features(sb_config_t, const char**, size_t, int)
_lib.sb_config_set_score_features.restype = None
_lib.sb_config_set_score_features.argtypes = [
    sb_config_t,
    POINTER(c_char_p),
    c_size_t,
    c_int,
]

# void sb_config_set_encrypted_key(sb_config_t, const char*)
_lib.sb_config_set_encrypted_key.restype = None
_lib.sb_config_set_encrypted_key.argtypes = [sb_config_t, c_char_p]

# void sb_config_set_signer(sb_config_t, sb_signer_callback_t, void*)
_lib.sb_config_set_signer.restype = None
_lib.sb_config_set_signer.argtypes = [sb_config_t, sb_signer_callback_t, c_void_p]

# void sb_config_set_scorbitd_version(sb_config_t, const char*)
_lib.sb_config_set_scorbitd_version.restype = None
_lib.sb_config_set_scorbitd_version.argtypes = [sb_config_t, c_char_p]

# void sb_config_set_scorbitd_platform_id(sb_config_t, const char*)
_lib.sb_config_set_scorbitd_platform_id.restype = None
_lib.sb_config_set_scorbitd_platform_id.argtypes = [sb_config_t, c_char_p]

# void sb_config_set_machine_title(sb_config_t, const char*)
_lib.sb_config_set_machine_title.restype = None
_lib.sb_config_set_machine_title.argtypes = [sb_config_t, c_char_p]

# void sb_config_set_extra_fingerprint(sb_config_t, const char*)
_lib.sb_config_set_extra_fingerprint.restype = None
_lib.sb_config_set_extra_fingerprint.argtypes = [sb_config_t, c_char_p]

# void sb_config_set_event_callback(sb_config_t, sb_event_callback_t, void*)
_lib.sb_config_set_event_callback.restype = None
_lib.sb_config_set_event_callback.argtypes = [sb_config_t, sb_event_callback_t, c_void_p]

# void sb_config_set_save_key_callback(sb_config_t, sb_save_key_callback_t, void*)
_lib.sb_config_set_save_key_callback.restype = None
_lib.sb_config_set_save_key_callback.argtypes = [sb_config_t, sb_save_key_callback_t, c_void_p]

# void sb_config_set_load_key_callback(sb_config_t, sb_load_key_callback_t, void*)
_lib.sb_config_set_load_key_callback.restype = None
_lib.sb_config_set_load_key_callback.argtypes = [sb_config_t, sb_load_key_callback_t, c_void_p]

# ---------------------------------------------------------------------------
# game_state_c.h
# ---------------------------------------------------------------------------

# sb_game_handle_t sb_create_game_state(sb_config_t)
_lib.sb_create_game_state.restype = sb_game_handle_t
_lib.sb_create_game_state.argtypes = [sb_config_t]

# void sb_destroy_game_state(sb_game_handle_t)
_lib.sb_destroy_game_state.restype = None
_lib.sb_destroy_game_state.argtypes = [sb_game_handle_t]

# void sb_set_game_started(sb_game_handle_t, sb_game_start_origin_t)
_lib.sb_set_game_started.restype = None
_lib.sb_set_game_started.argtypes = [sb_game_handle_t, c_int]

# void sb_set_game_finished(sb_game_handle_t)
_lib.sb_set_game_finished.restype = None
_lib.sb_set_game_finished.argtypes = [sb_game_handle_t]

# void sb_set_current_ball(sb_game_handle_t, sb_ball_t)
_lib.sb_set_current_ball.restype = None
_lib.sb_set_current_ball.argtypes = [sb_game_handle_t, c_uint]

# void sb_set_active_player(sb_game_handle_t, sb_player_t)
_lib.sb_set_active_player.restype = None
_lib.sb_set_active_player.argtypes = [sb_game_handle_t, c_uint]

# void sb_set_score(sb_game_handle_t, sb_player_t, sb_score_t, sb_score_feature_t)
_lib.sb_set_score.restype = None
_lib.sb_set_score.argtypes = [sb_game_handle_t, c_uint, c_int64, c_int32]

# void sb_add_mode(sb_game_handle_t, const char*)
_lib.sb_add_mode.restype = None
_lib.sb_add_mode.argtypes = [sb_game_handle_t, c_char_p]

# void sb_add_mode_expiring(sb_game_handle_t, const char*, uint32_t)
_lib.sb_add_mode_expiring.restype = None
_lib.sb_add_mode_expiring.argtypes = [sb_game_handle_t, c_char_p, c_uint32]

# void sb_remove_mode(sb_game_handle_t, const char*)
_lib.sb_remove_mode.restype = None
_lib.sb_remove_mode.argtypes = [sb_game_handle_t, c_char_p]

# void sb_clear_modes(sb_game_handle_t)
_lib.sb_clear_modes.restype = None
_lib.sb_clear_modes.argtypes = [sb_game_handle_t]

# void sb_commit(sb_game_handle_t)
_lib.sb_commit.restype = None
_lib.sb_commit.argtypes = [sb_game_handle_t]

# sb_auth_status_t sb_get_status(sb_game_handle_t)
_lib.sb_get_status.restype = c_int
_lib.sb_get_status.argtypes = [sb_game_handle_t]

# const char* sb_get_machine_uuid(sb_game_handle_t)
_lib.sb_get_machine_uuid.restype = c_char_p
_lib.sb_get_machine_uuid.argtypes = [sb_game_handle_t]

# uint64_t sb_get_machine_serial(sb_game_handle_t)
_lib.sb_get_machine_serial.restype = c_uint64
_lib.sb_get_machine_serial.argtypes = [sb_game_handle_t]

# const char* sb_get_pair_deeplink(sb_game_handle_t)
_lib.sb_get_pair_deeplink.restype = c_char_p
_lib.sb_get_pair_deeplink.argtypes = [sb_game_handle_t]

# void sb_request_top_scores(sb_game_handle_t, sb_score_t, sb_string_callback_t, void*)
_lib.sb_request_top_scores.restype = None
_lib.sb_request_top_scores.argtypes = [
    sb_game_handle_t, c_int64, sb_string_callback_t, c_void_p
]

# void sb_request_pair_code(sb_game_handle_t, sb_string_callback_t, void*)
_lib.sb_request_pair_code.restype = None
_lib.sb_request_pair_code.argtypes = [sb_game_handle_t, sb_string_callback_t, c_void_p]

# void sb_request_unpair(sb_game_handle_t, sb_string_callback_t, void*)
_lib.sb_request_unpair.restype = None
_lib.sb_request_unpair.argtypes = [sb_game_handle_t, sb_string_callback_t, c_void_p]

# void sb_set_capabilities(sb_game_handle_t, sb_capabilities_t)
_lib.sb_set_capabilities.restype = None
_lib.sb_set_capabilities.argtypes = [sb_game_handle_t, c_uint32]

# void sb_set_credits_dropped(sb_game_handle_t, int, const char*, bool)
_lib.sb_set_credits_dropped.restype = None
_lib.sb_set_credits_dropped.argtypes = [sb_game_handle_t, c_int, c_char_p, c_bool]

# void sb_set_credits_status(sb_game_handle_t, bool, int, int, const char*)
_lib.sb_set_credits_status.restype = None
_lib.sb_set_credits_status.argtypes = [sb_game_handle_t, c_bool, c_int, c_int, c_char_p]

# void sb_game_request_pair_machine(sb_game_handle_t, const char*, const char*,
#                                    sb_string_callback_t, void*)
_lib.sb_game_request_pair_machine.restype = None
_lib.sb_game_request_pair_machine.argtypes = [
    sb_game_handle_t, c_char_p, c_char_p, sb_string_callback_t, c_void_p
]

# void sb_download(sb_game_handle_t, const char*, const char*, const char*,
#                   sb_string_callback_t, void*)
_lib.sb_download.restype = None
_lib.sb_download.argtypes = [
    sb_game_handle_t, c_char_p, c_char_p, c_char_p, sb_string_callback_t, c_void_p
]

# void sb_download_buffer(sb_game_handle_t, const char*, size_t, const char*,
#                          sb_buffer_callback_t, void*)
_lib.sb_download_buffer.restype = None
_lib.sb_download_buffer.argtypes = [
    sb_game_handle_t, c_char_p, c_size_t, c_char_p, sb_buffer_callback_t, c_void_p
]

# ---------------------------------------------------------------------------
# event_helpers_c.h
# ---------------------------------------------------------------------------

# sb_event_type_t sb_event_type(const sb_event_t*)
_lib.sb_event_type.restype = c_int
_lib.sb_event_type.argtypes = [c_void_p]

# bool sb_event_game_start_requested(const sb_event_t*, int*)
_lib.sb_event_game_start_requested.restype = c_bool
_lib.sb_event_game_start_requested.argtypes = [c_void_p, POINTER(c_int)]

# bool sb_event_credits_add_requested(const sb_event_t*, int*, const char**)
_lib.sb_event_credits_add_requested.restype = c_bool
_lib.sb_event_credits_add_requested.argtypes = [c_void_p, POINTER(c_int), POINTER(c_char_p)]

# bool sb_event_players_updated(const sb_event_t*, int*)
_lib.sb_event_players_updated.restype = c_bool
_lib.sb_event_players_updated.argtypes = [c_void_p, POINTER(c_int)]

# bool sb_event_player_has_info(const sb_event_t*, sb_player_t, bool*)
_lib.sb_event_player_has_info.restype = c_bool
_lib.sb_event_player_has_info.argtypes = [c_void_p, c_uint, POINTER(c_bool)]

# bool sb_event_player_id(const sb_event_t*, sb_player_t, const char**)
_lib.sb_event_player_id.restype = c_bool
_lib.sb_event_player_id.argtypes = [c_void_p, c_uint, POINTER(c_char_p)]

# bool sb_event_player_preferred_name(const sb_event_t*, sb_player_t, const char**)
_lib.sb_event_player_preferred_name.restype = c_bool
_lib.sb_event_player_preferred_name.argtypes = [c_void_p, c_uint, POINTER(c_char_p)]

# bool sb_event_player_name(const sb_event_t*, sb_player_t, const char**)
_lib.sb_event_player_name.restype = c_bool
_lib.sb_event_player_name.argtypes = [c_void_p, c_uint, POINTER(c_char_p)]

# bool sb_event_player_initials(const sb_event_t*, sb_player_t, const char**)
_lib.sb_event_player_initials.restype = c_bool
_lib.sb_event_player_initials.argtypes = [c_void_p, c_uint, POINTER(c_char_p)]

# bool sb_event_player_picture_url(const sb_event_t*, sb_player_t, const char**)
_lib.sb_event_player_picture_url.restype = c_bool
_lib.sb_event_player_picture_url.argtypes = [c_void_p, c_uint, POINTER(c_char_p)]

# bool sb_event_player_claim_deeplink(const sb_event_t*, sb_player_t, const char**)
_lib.sb_event_player_claim_deeplink.restype = c_bool
_lib.sb_event_player_claim_deeplink.argtypes = [c_void_p, c_uint, POINTER(c_char_p)]

# bool sb_event_player_picture_ready(const sb_event_t*, sb_player_t*, const uint8_t**, size_t*)
_lib.sb_event_player_picture_ready.restype = c_bool
_lib.sb_event_player_picture_ready.argtypes = [
    c_void_p, POINTER(c_uint), POINTER(POINTER(c_uint8)), POINTER(c_size_t)
]

# bool sb_event_config_received(const sb_event_t*, const char**)
_lib.sb_event_config_received.restype = c_bool
_lib.sb_event_config_received.argtypes = [c_void_p, POINTER(c_char_p)]

# bool sb_event_scorbitd_update_received(const sb_event_t*, const char**)
_lib.sb_event_scorbitd_update_received.restype = c_bool
_lib.sb_event_scorbitd_update_received.argtypes = [c_void_p, POINTER(c_char_p)]

# bool sb_event_scorbitd_updated(const sb_event_t*, const char**, const char**)
_lib.sb_event_scorbitd_updated.restype = c_bool
_lib.sb_event_scorbitd_updated.argtypes = [c_void_p, POINTER(c_char_p), POINTER(c_char_p)]

# bool sb_event_firmwares_list_received(const sb_event_t*, const char**)
_lib.sb_event_firmwares_list_received.restype = c_bool
_lib.sb_event_firmwares_list_received.argtypes = [c_void_p, POINTER(c_char_p)]

# bool sb_event_diagnostics_upload_requested(const sb_event_t*, bool*)
_lib.sb_event_diagnostics_upload_requested.restype = c_bool
_lib.sb_event_diagnostics_upload_requested.argtypes = [c_void_p, POINTER(c_bool)]

# bool sb_event_diagnostics_uploaded(const sb_event_t*, bool*)
_lib.sb_event_diagnostics_uploaded.restype = c_bool
_lib.sb_event_diagnostics_uploaded.argtypes = [c_void_p, POINTER(c_bool)]

# -- Pricing event helpers --

# bool sb_event_pricing_free_play(const sb_event_t*, bool*)
_lib.sb_event_pricing_free_play.restype = c_bool
_lib.sb_event_pricing_free_play.argtypes = [c_void_p, POINTER(c_bool)]

# bool sb_event_pricing_payments_enabled(const sb_event_t*, bool*)
_lib.sb_event_pricing_payments_enabled.restype = c_bool
_lib.sb_event_pricing_payments_enabled.argtypes = [c_void_p, POINTER(c_bool)]

# bool sb_event_pricing_credit_price(const sb_event_t*, const char**)
_lib.sb_event_pricing_credit_price.restype = c_bool
_lib.sb_event_pricing_credit_price.argtypes = [c_void_p, POINTER(c_char_p)]

# bool sb_event_pricing_credit_regular_price(const sb_event_t*, const char**)
_lib.sb_event_pricing_credit_regular_price.restype = c_bool
_lib.sb_event_pricing_credit_regular_price.argtypes = [c_void_p, POINTER(c_char_p)]

# bool sb_event_pricing_credit_sale_price(const sb_event_t*, const char**)
_lib.sb_event_pricing_credit_sale_price.restype = c_bool
_lib.sb_event_pricing_credit_sale_price.argtypes = [c_void_p, POINTER(c_char_p)]

# bool sb_event_pricing_bundles_count(const sb_event_t*, int*)
_lib.sb_event_pricing_bundles_count.restype = c_bool
_lib.sb_event_pricing_bundles_count.argtypes = [c_void_p, POINTER(c_int)]

# bool sb_event_pricing_bundle_credits(const sb_event_t*, int, int*)
_lib.sb_event_pricing_bundle_credits.restype = c_bool
_lib.sb_event_pricing_bundle_credits.argtypes = [c_void_p, c_int, POINTER(c_int)]

# bool sb_event_pricing_bundle_price(const sb_event_t*, int, const char**)
_lib.sb_event_pricing_bundle_price.restype = c_bool
_lib.sb_event_pricing_bundle_price.argtypes = [c_void_p, c_int, POINTER(c_char_p)]

# bool sb_event_pricing_bundle_regular_price(const sb_event_t*, int, const char**)
_lib.sb_event_pricing_bundle_regular_price.restype = c_bool
_lib.sb_event_pricing_bundle_regular_price.argtypes = [c_void_p, c_int, POINTER(c_char_p)]

# bool sb_event_pricing_bundle_sale_price(const sb_event_t*, int, const char**)
_lib.sb_event_pricing_bundle_sale_price.restype = c_bool
_lib.sb_event_pricing_bundle_sale_price.argtypes = [c_void_p, c_int, POINTER(c_char_p)]

# void sb_upload_diagnostics(sb_game_handle_t, const char**, size_t,
#                             const char**, size_t, const char*)
_lib.sb_upload_diagnostics.restype = None
_lib.sb_upload_diagnostics.argtypes = [
    sb_game_handle_t, POINTER(c_char_p), c_size_t,
    POINTER(c_char_p), c_size_t, c_char_p
]

# ---------------------------------------------------------------------------
# log_c.h (sb_add_logger_callback / sb_reset_logger; no-ops when SDK uses spdlog)
# ---------------------------------------------------------------------------

_has_logger = False
if hasattr(_lib, "sb_add_logger_callback"):
    if hasattr(_lib, "sb_logger_callbacks_supported"):
        _lib.sb_logger_callbacks_supported.restype = c_bool
        _lib.sb_logger_callbacks_supported.argtypes = []
        _has_logger = bool(_lib.sb_logger_callbacks_supported())
    else:
        # Older SDKs: symbol exists but we cannot tell spdlog stub vs callback at runtime.
        _has_logger = True

if _has_logger:
    # void sb_add_logger_callback(sb_log_callback_t, void*, size_t)
    _lib.sb_add_logger_callback.restype = None
    _lib.sb_add_logger_callback.argtypes = [sb_log_callback_t, c_void_p, c_size_t]

    # void sb_reset_logger(void)
    _lib.sb_reset_logger.restype = None
    _lib.sb_reset_logger.argtypes = []
