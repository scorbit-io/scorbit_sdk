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

// URLs
constexpr auto URL_API = "api";

constexpr auto URL_SCORBITRON_TOKEN {"v2/scorbitrons/{scorbitron_uuid}/token/"};
constexpr auto URL_SCORBITRON_CF_TOKEN {"v2/scorbitrons/{scorbitron_uuid}/socket/"};
constexpr auto URL_SCORBITRON_CONFIG {"v2/scorbitrons/{scorbitron_uuid}/config/"};
constexpr auto URL_SCORBITRON_SESSIONS {"v2/scorbitrons/{scorbitron_uuid}/sessions/"};
constexpr auto URL_SCORBITRON_SESSION_UPDATE {"v2/scorbitrons/{scorbitron_uuid}/sessions/{session_uuid}/"};
constexpr auto URL_SCORBITRON_OBJECT {"v2/scorbitrons/{scorbitron_uuid}/"};

constexpr auto URL_CENTRIFUGO {"connection/websocket"};

// Hostnames
constexpr auto PRODUCTION_LABEL = "production";
constexpr auto PRODUCTION_HOSTNAME = "https://api.scorbit.io";
constexpr auto PRODUCTION_CENTRIFUGO = "wss://centrifuge.scorbit.io";

constexpr auto STAGING_LABEL = "staging";
constexpr auto STAGING_HOSTNAME = "https://staging.scorbit.io";
constexpr auto STAGING_CENTRIFUGO = "wss://sws.scorbit.io";

// Headers
constexpr auto HDR_KEY_AUTHORIZATION {"Authorization"};
constexpr auto HDR_VAL_BEARER {"Bearer "}; // note the space at the end

constexpr auto HDR_KEY_CONTENT_TYPE {"Content-Type"};
constexpr auto HDR_VAL_CONTENT_JSON {"application/json"};
constexpr auto HDR_VAL_CONTENT_MULTIPART {"multipart/form-data"};

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
constexpr auto JKEY_SCR_PLAYER {"player"};
constexpr auto JKEY_SCR_SCORE {"score"};
constexpr auto JKEY_SCR_BALL {"ball"};
constexpr auto JKEY_SCR_BALL_IN_PROGRESS {"ball_in_progress"};
constexpr auto JKEY_SCR_MODES {"modes"};

// Channel's message
constexpr auto JKEY_CHN_TYPE {"type"};
constexpr auto JKEY_CHN_PAYLOAD {"payload"};
constexpr auto JVAL_CHN_TYPE_START_GAME {"start_game"};

// Score update payload
constexpr auto JKEY_SCR_GAME_IN_PROGRESS {"game_in_progress"};
constexpr auto JKEY_SCR_SCORES {"scores"};
constexpr auto JKEY_SCR_METADATA {"metadata"};
constexpr auto JKEY_SCR_GAME {"game"};
constexpr auto JKEY_SCR_MACHINE {"machine"};
constexpr auto JKEY_SCR_VARIANT {"variant"};
constexpr auto JKEY_SCR_SEQUENCE {"sequence"};
constexpr auto JKEY_SCR_TIMESTAMP {"timestamp"};

constexpr auto JKEY_SCR_SCORE_UPDATE {"score_update"};

// Scorbitron config
constexpr auto JKEY_SCFG_IS_PAIRED {"is_paired"};
constexpr auto JKEY_SCFG_SHORTCODE {"shortcode"};
constexpr auto JKEY_SCFG_MACHINE_UUID {"machine_id"};
constexpr auto JKEY_SCFG_MACHINE_ID {"machine_id"};
constexpr auto JKEY_SCFG_VARIANT_ID {"variant_id"};
constexpr auto JKEY_SCFG_CONFIG {"config"};
constexpr auto JKEY_SCFG_OPDB_ID {"opdb_id"};
constexpr auto JKEY_SCFG_SCORBITRON_MACHINE {"machine"};

constexpr auto JKEY_SCFG_VERSION {"version"};
constexpr auto JKEY_SCFG_TYPE {"type"};
constexpr auto JKEY_SCFG_INSTALLED {"installed"};
constexpr auto JKEY_SCFG_LOG {"log"};

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


/*


 */

} // namespace detail
} // namespace scorbit
