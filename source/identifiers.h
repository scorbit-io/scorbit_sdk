/****************************************************************************
 *
 * @author Dilshod Mukhtarov <dilshodm(at)gmail.com>
 * Sep 2025
 *
 ****************************************************************************/

#pragma once

namespace scorbit {
namespace detail {

constexpr auto ARG_SCORBITRON_UUID {"scorbitron_uuid"};
constexpr auto ARG_MACHINE_UUID {"machine_uuid"};
constexpr auto ARG_VARIANT_UUID {"variant_uuid"};
constexpr auto ARG_GAME_SLUG {"game_slug"};
constexpr auto ARG_SESSION_UUID {"session_uuid"};
constexpr auto ARG_SCORE_ID {"score_id"};

// URLs
constexpr auto NOOP_URL {"http://api.scorbit.io/api/noop/"};

constexpr auto URL_SCORBITRON_TOKEN {"api/v2/scorbitrons/{scorbitron_uuid}/token/"};
constexpr auto URL_SCORBITRON_CF_TOKEN {"api/v2/scorbitrons/{scorbitron_uuid}/socket/"};
constexpr auto URL_SCORBITRON_CONFIG {"api/v2/scorbitrons/{scorbitron_uuid}/config/"};
constexpr auto URL_SCORBITRON_SESSIONS {"api/v2/scorbitrons/{scorbitron_uuid}/sessions/"};
constexpr auto URL_SCORBITRON_SESSION_UPDATE {
        "api/v2/scorbitrons/{scorbitron_uuid}/sessions/{session_uuid}/"};
constexpr auto URL_SCORBITRON_OBJECT {"api/v2/scorbitrons/{scorbitron_uuid}/"};
constexpr auto URL_SCORBITRON_NFC_NONCE_CREATE {"api/v2/scorbitrons/{scorbitron_uuid}/nonce/"};
constexpr auto URL_SCORBITRON_CREDIT_DROP_CREATE {
        "api/v2/scorbitrons/{scorbitron_uuid}/credit-drop/"};
constexpr auto URL_SCORBITRON_FIRMWARES_LIST {"api/v2/scorbitrons/{scorbitron_uuid}/firmwares/"};
constexpr auto URL_MACHINE_OBJECT {"api/v2/machines/{machine_uuid}/"};
constexpr auto URL_MACHINE_LEADERS {"api/v2/machines/{machine_uuid}/leaders/"};
constexpr auto URL_VARIANT_LEADERS {"api/v2/variants/{variant_uuid}/leaders/"};
constexpr auto URL_GAME_LEADERS {"api/v2/games/{game_slug}/leaders/"};
constexpr auto URL_SCORBITRON_DIAGNOSTICS {"api/v2/scorbitrons/{scorbitron_uuid}/diagnostics/"};
constexpr auto URL_DIAGNOSTICS_ACK_PATH {"internal/api/diagnostics/ack/"};

constexpr auto URL_V2_PROVISION {"api/v2/provision/"};

constexpr auto URL_NFC_TAG {"https://scorbit.link/machines/{machine_uuid}?n={nonce}"};
constexpr auto URL_CLAIM_DEEPLINK {
        "https://scorbit.link/machines/{machine_uuid}/?score_id={score_id}"};

constexpr auto URL_SESSIONS_ID {"sessions"};

constexpr auto URL_CENTRIFUGO {"connection/websocket"};

// Hostnames
constexpr auto PRODUCTION_LABEL = "production";
constexpr auto PRODUCTION_HOSTNAME = "https://api.scorbit.io";
constexpr auto PRODUCTION_CENTRIFUGO = "wss://sws.scorbit.io";

constexpr auto STAGING_LABEL = "staging";
constexpr auto STAGING_HOSTNAME = "https://staging.scorbit.io";
constexpr auto STAGING_CENTRIFUGO = "wss://sws.scorbit.io";

// Headers
constexpr auto HDR_KEY_AUTHORIZATION {"Authorization"};
constexpr auto HDR_VAL_BEARER {"Bearer "}; // note the space at the end

constexpr auto HDR_KEY_ACCEPT_CONTENT {"Accept"};
constexpr auto HDR_KEY_CONTENT_TYPE {"Content-Type"};
constexpr auto HDR_VAL_CONTENT_JSON {"application/json"};
constexpr auto HDR_VAL_CONTENT_MULTIPART {"multipart/form-data"};
constexpr auto HDR_VAL_CONTENT_OCTET {"application/octet-stream"};

constexpr auto HDR_KEY_CACHE_CONTROL {"Cache-Control"};
constexpr auto HDR_VAL_NO_CACHE {"no-cache"};

constexpr auto HDR_KEY_FINGERPRINT_HASH {"X-Fingerprint-Hash"};

// Providers
constexpr auto PROVIDER_SCORBITRON {"scorbitron"};
constexpr auto PROVIDER_VSCORBITRON {"vscorbitron"};

// Protocols
constexpr auto PROTO_HTTPS {"https"};
constexpr auto PROTO_HTTP {"http"};
constexpr auto PROTO_WSS {"wss"};
constexpr auto PROTO_WS {"ws"};

// Rest
constexpr auto REST_GET {"GET"};
constexpr auto REST_POST {"POST"};
constexpr auto REST_PATCH {"PATCH"};
constexpr auto REST_PUT {"PUT"};

// Centrifugo
constexpr auto CF_CHN_MACHINE {"machine"};
constexpr auto CF_CHN_CONTROL_MACHINE {"control_machine"};
constexpr auto CF_CHN_CONTROL_SCORBITRON {"control_scorbitron"};

// ------------------------------- JSON -------------------------------

// Authentication
constexpr auto JKEY_SCORBITRON_TOKEN {"token"};
constexpr auto JKEY_CF_TOKEN {"token"};
constexpr auto JKEY_USERNAME {"username"};
constexpr auto JKEY_AVATAR {"avatar"};

// Authentication
constexpr auto JKEY_AUTH_PROVIDER {"provider"};
constexpr auto JKEY_AUTH_UUID {"uuid"};
constexpr auto JKEY_AUTH_TIMESTAMP {"timestamp"};
constexpr auto JKEY_AUTH_SIGNATURE {"signature"};
constexpr auto JKEY_AUTH_SERIAL_NUMBER {"serial_number"};

// Scores
constexpr auto JKEY_SCR_POSITION {"position"};
constexpr auto JKEY_SCR_ID {"id"};
constexpr auto JKEY_SCR_IS_NFC_VERIFIED {"is_nfc_verified"};
constexpr auto JKEY_SCR_TOURNAMENT_UUID {"tournament_id"};
constexpr auto JKEY_SCR_PLAYER {"player"};
constexpr auto JKEY_SCR_SCORE {"score"};
constexpr auto JKEY_SCR_BALL {"ball"};
constexpr auto JKEY_SCR_BALL_IN_PROGRESS {"ball_in_progress"};
constexpr auto JKEY_SCR_MODES {"modes"};

// Channel's message
constexpr auto JKEY_CHN_TYPE {"type"};
constexpr auto JKEY_CHN_PAYLOAD {"payload"};
constexpr auto JVAL_CHN_TYPE_START_GAME {"start_game"};
constexpr auto JVAL_CHN_TYPE_ADD_CREDITS {"add_credits"};
constexpr auto JVAL_CHN_TYPE_DIAG_PROBE {"diag_probe"};

// Score update payload
constexpr auto JKEY_SCR_GAME_IN_PROGRESS {"game_in_progress"};
constexpr auto JKEY_SCR_SCORES {"scores"};
constexpr auto JKEY_SCR_FINAL_SCORES {"final_scores"};
constexpr auto JKEY_SCR_METADATA {"metadata"};
constexpr auto JKEY_SCR_GAME {"game"};
constexpr auto JKEY_SCR_MACHINE {"machine"};
constexpr auto JKEY_SCR_VARIANT {"variant"};
constexpr auto JKEY_SCR_VENUE {"venue"};
constexpr auto JKEY_SCR_SEQUENCE {"sequence"};
constexpr auto JKEY_SCR_CREATED_AT {"created_at"};
constexpr auto JKEY_SCR_UPDATED_AT {"updated_at"};

constexpr auto JVAL_SCR_SCORE_UPDATE {"score_update"};
constexpr auto JVAL_SCR_GAME_END {"game_end"};

// Diagnostic probe (SB-3363) — handler-side keys for the inbound probe on
// control_machine, the tagged packet republished to machine:<uuid>, and the
// device_egress ack POSTed to /internal/api/diagnostics/ack/.
constexpr auto JKEY_DIAG_TRACE_ID {"trace_id"};
constexpr auto JKEY_DIAG_DEADLINE_SECONDS {"deadline_seconds"};
constexpr auto JKEY_DIAG_HOP {"hop"};
constexpr auto JKEY_DIAG_TS {"ts"};
constexpr auto JKEY_DIAG_PAYLOAD {"payload"};
constexpr auto JKEY_DIAG_SEQUENCE {"sequence"};
constexpr auto JVAL_DIAG_HOP_DEVICE_EGRESS {"device_egress"};

// Scorbitron config
constexpr auto JKEY_SCFG_MACHINE_ID {"machine_id"};
constexpr auto JKEY_SCFG_VARIANT_ID {"variant_id"};
constexpr auto JKEY_SCFG_VENUE_ID {"venue_id"};
constexpr auto JKEY_SCFG_CONFIG {"config"};
constexpr auto JKEY_SCFG_OPDB_ID {"opdb_id"};
constexpr auto JKEY_SCFG_SCORBITRON_MACHINE {"machine"};
constexpr auto JKEY_SCFG_OWNER {"owner"};
constexpr auto JKEY_SCFG_PRICING {"pricing"};

constexpr auto JKEY_SCFG_VERSION {"version"};
constexpr auto JKEY_SCFG_TYPE {"type"};
constexpr auto JKEY_SCFG_INSTALLED {"installed"};
constexpr auto JKEY_SCFG_LOG {"log"};

// Scorbitron ojbect
constexpr auto JKEY_SOBJ_SHORTCODE {"shortcode"};
constexpr auto JKEY_SOBJ_MACHINE_OBJ {"machine"};
constexpr auto JKEY_SOBJ_MACHINE_UUID {"id"};
constexpr auto JKEY_SOBJ_SDK_VERSION {"sdk_version"};
constexpr auto JKEY_SOBJ_SCORBITD_VERSION {"scorbitd_version"};
constexpr auto JKEY_SOBJ_GAME_CODE_VERSION {"game_code_version"};
constexpr auto JKEY_SOBJ_START_GAME_CAPABLE {"start_game_capable"};
constexpr auto JKEY_SOBJ_NFC_CAPABLE {"nfc_capable"};
constexpr auto JKEY_SOBJ_CREDIT_DROP_CAPABLE {"credit_drop_capable"};
constexpr auto JKEY_SOBJ_DIAG_PROBE_CAPABLE {"diag_probe_capable"};
constexpr auto JKEY_SOBJ_LAN_IP {"lan_ip"};

constexpr auto JKEY_SOBJ_RELEASE_TRACK {"release_track"};
constexpr auto JKEY_SOBJ_RELEASE_URL {"url"};

// Machine object
constexpr auto JKEY_MOBJ_NAME {"name"};
constexpr auto JKEY_MOBJ_EDITION {"edition"};
constexpr auto JKEY_MOBJ_GAME {"game"};
constexpr auto JKEY_MOBJ_SLUG {"slug"};

// Session
constexpr auto JKEY_SESS_UUID {"id"};
constexpr auto JKEY_SESS_PLAYER_COUNT {"player_count"};
constexpr auto JKEY_SESS_SEQUENCE_NUMBER {"sequence_number"};
constexpr auto JKEY_SESS_SESSION_TIME {"session_time"};
constexpr auto JKEY_SESS_ACTIVE_ON {"active_on"};
constexpr auto JKEY_SESS_SETTLED_ON {"settled_on"};
constexpr auto JKEY_SESS_SUCCESSFULLY_COMPLETED {"successfully_completed"};
constexpr auto JKEY_SESS_USE_LOBBY {"use_lobby"};
constexpr auto JKEY_SESS_LOG_FILE {"file"};

// Session log (CSV)
constexpr auto JKEY_SESS_LOG_UUID {"uuid"};
constexpr auto JKEY_SESS_LOG_FILE_DEPRECATED {"log_file"};

constexpr auto SESS_LOG_EXTENSION {"csv"};

// Player's profile
constexpr auto JKEY_PLAYER_ID {"id"};
constexpr auto JKEY_PLAYER_PREFER_INITIALS {"prefer_initials"};
constexpr auto JKEY_PLAYER_USERNAME {"username"};
constexpr auto JKEY_PLAYER_DISPLAY_NAME {"display_name"};
constexpr auto JKEY_PLAYER_INITIALS {"initials"};
constexpr auto JKEY_PLAYER_URL {"url"};
constexpr auto JKEY_PLAYER_AVATAR {"avatar"};

// Types
constexpr auto JVAL_TYPE_ACTION {"action"};
constexpr auto JKEY_METHOD {"method"};
constexpr auto JVAL_METHOD_GET {"GET"};
constexpr auto JVAL_METHOD_MSG {"MSG"};
constexpr auto JVAL_METHOD_SIGNAL {"SIGNAL"};
constexpr auto JKEY_URL {"url"};

constexpr auto JKEY_ACTION_NAME {"name"};
constexpr auto JVAL_ACITON_GET_SCORBITRON_SESSION {"scorbitronSessionRetreive"};
constexpr auto JVAL_ACTION_UPLOAD_DIAGNOSTICS {"scorbitronUploadDiagnostics"};
constexpr auto JVAL_ACTION_CONFIG_REFRESH {"scorbitronConfigRefresh"};
constexpr auto JVAL_ACTION_SCORBITRON_PAIRED {"scorbitronPaired"};
constexpr auto JVAL_ACTION_SCORBITRON_UNPAIRED {"scorbitronUnpaired"};

constexpr auto JVAL_NONCES {"nonces"};

// Credits
constexpr auto JKEY_CREDITS_COUNT {"credits"};
constexpr auto JKEY_CREDITS_DROPPED {"successful_credits"};
constexpr auto JKEY_CREDITS_TRANSACTION {"transaction"};
constexpr auto JKEY_CREDITS_SUCCESS {"success"};

constexpr auto JVAL_CURRENT_MACHINE_STATE {"current_machine_state"};
constexpr auto JKEY_CREDITS_STATE {"current_machine_state"};
constexpr auto JKEY_CREDITS_FREE_PLAY {"free_play"};
constexpr auto JKEY_CREDITS_CURRENT {"credits"};
constexpr auto JKEY_CREDITS_MAX {"max_credits"};
constexpr auto JKEY_CREDITS_PRICING {"pricing"};

} // namespace detail
} // namespace scorbit
