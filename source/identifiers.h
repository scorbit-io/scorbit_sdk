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
constexpr auto ARG_SESSION_UUID {"session_uuid"};
constexpr auto ARG_MACHINE_ID {"machine_id"};

// URLs
constexpr auto NOOP_URL {"http://api.scorbit.io/api/noop/"};

constexpr auto URL_API = "api";

constexpr auto URL_SCORBITRON_TOKEN {"v2/scorbitrons/{scorbitron_uuid}/token/"};
constexpr auto URL_SCORBITRON_CF_TOKEN {"v2/scorbitrons/{scorbitron_uuid}/socket/"};
constexpr auto URL_SCORBITRON_CONFIG {"v2/scorbitrons/{scorbitron_uuid}/config/"};
constexpr auto URL_SCORBITRON_SESSIONS {"v2/scorbitrons/{scorbitron_uuid}/sessions/"};
constexpr auto URL_SCORBITRON_SESSION_UPDATE {
        "v2/scorbitrons/{scorbitron_uuid}/sessions/{session_uuid}/"};
constexpr auto URL_SCORBITRON_OBJECT {"v2/scorbitrons/{scorbitron_uuid}/"};
constexpr auto URL_SCORBITRON_NFC_NONCE_CREATE {"v2/scorbitrons/{scorbitron_uuid}/nonce/"};
constexpr auto URL_SCORBITRON_CREDIT_DROP_CREATE {"v2/scorbitrons/{scorbitron_uuid}/credit-drop/"};

constexpr auto URL_NFC_TAG {"https://scorbit.link/machines/{machine_uuid}?n={nonce}"};

constexpr auto URL_SESSIONS_ID {"sessions"};

// Achievement REST API endpoints (v2)
constexpr auto URL_ACHIEVEMENTS_SCORBITRON {"v2/achievements/scorbitron/"};
constexpr auto URL_USER_ACHIEVEMENTS_SCORBITRON {"v2/achievements/scorbitron/progress/"};
constexpr auto URL_ACHIEVEMENT_UNLOCK {"v2/achievements/unlock/"};
constexpr auto URL_ACHIEVEMENT_LOCK {"v2/achievements/lock/"};

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
constexpr auto CF_CHN_ACHIEVEMENTS_SESSION {"achievements:session:"};

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

// Achievement event types
constexpr auto JVAL_CHN_TYPE_ACHIEVEMENT_UNLOCKED {"achievement_unlocked"};
constexpr auto JVAL_CHN_TYPE_ACHIEVEMENT_LOCKED {"achievement_locked"};
constexpr auto JVAL_CHN_TYPE_ACHIEVEMENT_PROGRESS {"achievement_progress"};

// Achievement payload keys (Centrifugo events)
constexpr auto JKEY_ACHIEVEMENT_KEY {"achievement_key"};
constexpr auto JKEY_ACHIEVEMENT_NAME {"achievement_name"};
constexpr auto JKEY_ACHIEVEMENT_USER_ID {"user_id"};
constexpr auto JKEY_ACHIEVEMENT_USERNAME {"username"};
constexpr auto JKEY_ACHIEVEMENT_ICON_URL {"icon_url"};
constexpr auto JKEY_ACHIEVEMENT_IS_TROPHY {"is_trophy"};
constexpr auto JKEY_ACHIEVEMENT_CURRENT_VALUE {"current_value"};
constexpr auto JKEY_ACHIEVEMENT_TARGET_VALUE {"target_value"};

// Achievement REST API keys
constexpr auto JKEY_ACH_KEY {"key"};
constexpr auto JKEY_ACH_NAME {"name"};
constexpr auto JKEY_ACH_DESCRIPTION {"description"};
constexpr auto JKEY_ACH_COUNT {"count"};
constexpr auto JKEY_ACH_IMAGE {"image"};
constexpr auto JKEY_ACH_OBSCURE_IMAGE {"obscure_image"};
constexpr auto JKEY_ACH_OBSCURE {"obscure"};
constexpr auto JKEY_ACH_INPUT_TIME {"input_time"};
constexpr auto JKEY_ACH_VISIBLE {"visible"};
constexpr auto JKEY_ACH_NOTIFY {"notify_when_achieved"};
constexpr auto JKEY_ACH_IS_TROPHY {"is_trophy"};
constexpr auto JKEY_ACH_GROUP_ID {"groupid"};
constexpr auto JKEY_ACH_ACHIEVEMENT_ID {"achievementid"};
constexpr auto JKEY_ACH_TRIGGER {"trigger"};
constexpr auto JKEY_ACH_MODE_NAME {"mode_name"};
constexpr auto JKEY_ACH_MODE_TYPE {"mode_type"};
constexpr auto JKEY_ACH_TARGET_SCORE {"target_score"};
constexpr auto JKEY_ACH_ACHIEVEMENTS {"achievements"};
constexpr auto JKEY_ACH_USER_ID {"user_id"};
constexpr auto JKEY_ACH_PROGRESS {"progress"};
constexpr auto JKEY_ACH_UNLOCKED {"unlocked"};
constexpr auto JKEY_ACH_UNLOCKED_AT {"unlocked_at"};
constexpr auto JVAL_ACH_INPUT_LIMITED {"limited"};
constexpr auto JVAL_ACH_INPUT_UNLIMITED {"unlimited"};

// Achievement v2 fields (replacing v1 flat fields)
constexpr auto JKEY_ACH_IS_SINGLE_SESSION {"is_single_session"};
constexpr auto JKEY_ACH_IS_BADGE {"is_badge"};
constexpr auto JKEY_ACH_SCOPE {"scope"};
constexpr auto JKEY_ACH_LEVEL {"level"};
constexpr auto JKEY_ACH_BALL_COUNT {"ball_count"};
constexpr auto JKEY_ACH_ICON {"icon"};

// Achievement v2 nested rule keys
constexpr auto JKEY_ACH_RULES {"rules"};
constexpr auto JKEY_ACH_RULE_TYPE {"type"};
constexpr auto JKEY_ACH_RULE_COMPARISON {"comparison"};
constexpr auto JKEY_ACH_RULE_TARGET {"target"};
constexpr auto JKEY_ACH_RULE_REFERENCE {"reference"};
constexpr auto JKEY_ACH_RULE_SUBACHIEVEMENT {"subachievement"};

// v2 user achievement / progress keys
constexpr auto JKEY_ACH_ACHIEVEMENT {"achievement"};
constexpr auto JKEY_ACH_CURRENT_VALUE {"current_value"};
constexpr auto JKEY_ACH_ACHIEVED {"achieved"};
constexpr auto JKEY_ACH_ACHIEVED_TIME {"achieved_time"};

// v2 unlock response keys
constexpr auto JKEY_ACH_NEWLY_UNLOCKED {"newly_unlocked"};
constexpr auto JKEY_ACH_USER_ACHIEVEMENT {"user_achievement"};

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

// Scorbitron config
constexpr auto JKEY_SCFG_MACHINE_ID {"machine_id"};
constexpr auto JKEY_SCFG_VARIANT_ID {"variant_id"};
constexpr auto JKEY_SCFG_VENUE_ID {"venue_id"};
constexpr auto JKEY_SCFG_CONFIG {"config"};
constexpr auto JKEY_SCFG_OPDB_ID {"opdb_id"};
constexpr auto JKEY_SCFG_SCORBITRON_MACHINE {"machine"};
constexpr auto JKEY_SCFG_OWNER {"owner"};

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

constexpr auto JKEY_SOBJ_RELEASE_TRACK {"release_track"};
constexpr auto JKEY_SOBJ_RELEASE_URL {"url"};

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
constexpr auto JKEY_URL {"url"};

constexpr auto JKEY_ACTION_NAME {"name"};
constexpr auto JVAL_ACITON_GET_SCORBITRON_SESSION {"scorbitronSessionRetrieve"};

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
