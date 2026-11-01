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

#include "scorbit_game_state.h"

#include "key_store.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstring>

using namespace godot;

namespace scorbit_godot {

namespace {

constexpr auto DEFAULT_KEY_PATH = "user://scorbit/device_key";

String toString(const char *str)
{
    return str ? String::utf8(str) : String();
}

// Event data is valid only during the callback, so everything is copied out here.
Dictionary readEvent(const sb_event_t *event)
{
    Dictionary data;
    const auto type = sb_event_type(event);
    data["type"] = static_cast<int>(type);

    switch (type) {
    case SB_EVT_PAIRING_STATUS_CHANGED: {
        bool isPaired = false;
        if (sb_event_pairing_status_changed(event, &isPaired)) {
            data["is_paired"] = isPaired;
        }
        break;
    }
    case SB_EVT_GAME_START_REQUESTED: {
        int players = 0;
        if (sb_event_game_start_requested(event, &players)) {
            data["players"] = players;
        }
        break;
    }
    case SB_EVT_CONFIG_RECEIVED: {
        const char *json = nullptr;
        if (sb_event_config_received(event, &json)) {
            data["config_json"] = toString(json);
        }
        break;
    }
    case SB_EVT_PLAYERS_UPDATED: {
        int count = 0;
        if (!sb_event_players_updated(event, &count)) {
            break;
        }
        Dictionary players;
        for (sb_player_t p = 1; p <= static_cast<sb_player_t>(count); ++p) {
            Dictionary info;
            bool hasInfo = false;
            sb_event_player_has_info(event, p, &hasInfo);
            info["has_info"] = hasInfo;
            const char *str = nullptr;
            if (sb_event_player_id(event, p, &str)) {
                info["id"] = toString(str);
            }
            if (sb_event_player_preferred_name(event, p, &str)) {
                info["preferred_name"] = toString(str);
            }
            if (sb_event_player_name(event, p, &str)) {
                info["name"] = toString(str);
            }
            if (sb_event_player_initials(event, p, &str)) {
                info["initials"] = toString(str);
            }
            if (sb_event_player_picture_url(event, p, &str)) {
                info["picture_url"] = toString(str);
            }
            if (sb_event_player_claim_deeplink(event, p, &str)) {
                info["claim_deeplink"] = toString(str);
            }
            players[static_cast<int>(p)] = info;
        }
        data["players"] = players;
        break;
    }
    case SB_EVT_CREDITS_ADD_REQUESTED: {
        int credits = 0;
        const char *transaction = nullptr;
        if (sb_event_credits_add_requested(event, &credits, &transaction)) {
            data["credits"] = credits;
            data["transaction"] = toString(transaction);
        }
        break;
    }
    case SB_EVT_PRICING_RECEIVED: {
        bool flag = false;
        if (sb_event_pricing_free_play(event, &flag)) {
            data["free_play"] = flag;
        }
        if (sb_event_pricing_payments_enabled(event, &flag)) {
            data["payments_enabled"] = flag;
        }
        const char *str = nullptr;
        if (sb_event_pricing_credit_price(event, &str)) {
            data["credit_price"] = toString(str);
        }
        if (sb_event_pricing_credit_regular_price(event, &str)) {
            data["credit_regular_price"] = toString(str);
        }
        if (sb_event_pricing_credit_sale_price(event, &str)) {
            data["credit_sale_price"] = toString(str);
        }
        int count = 0;
        sb_event_pricing_bundles_count(event, &count);
        Array bundles;
        for (int i = 0; i < count; ++i) {
            Dictionary bundle;
            int credits = 0;
            if (sb_event_pricing_bundle_credits(event, i, &credits)) {
                bundle["credits"] = credits;
            }
            if (sb_event_pricing_bundle_price(event, i, &str)) {
                bundle["price"] = toString(str);
            }
            if (sb_event_pricing_bundle_regular_price(event, i, &str)) {
                bundle["regular_price"] = toString(str);
            }
            if (sb_event_pricing_bundle_sale_price(event, i, &str)) {
                bundle["sale_price"] = toString(str);
            }
            bundles.push_back(bundle);
        }
        data["bundles"] = bundles;
        break;
    }
    case SB_EVT_DIAGNOSTICS_UPLOAD_REQUESTED: {
        bool includeRecordings = false;
        if (sb_event_diagnostics_upload_requested(event, &includeRecordings)) {
            data["include_recordings"] = includeRecordings;
        }
        break;
    }
    case SB_EVT_DIAGNOSTICS_UPLOADED: {
        bool success = false;
        if (sb_event_diagnostics_uploaded(event, &success)) {
            data["success"] = success;
        }
        break;
    }
    case SB_EVT_PLAYER_PICTURE_READY: {
        sb_player_t player = 0;
        const uint8_t *bytes = nullptr;
        size_t size = 0;
        if (sb_event_player_picture_ready(event, &player, &bytes, &size)) {
            PackedByteArray picture;
            picture.resize(static_cast<int64_t>(size));
            if (size > 0 && bytes) {
                std::memcpy(picture.ptrw(), bytes, size);
            }
            data["player"] = static_cast<int>(player);
            data["picture"] = picture;
        }
        break;
    }
    default:
        break;
    }
    return data;
}

// Every field of each entry, copied out: the leaderboard is valid only during its callback.
Array readLeaderboard(const sb_leaderboard_t *leaderboard)
{
    Array entries;
    const size_t count = sb_leaderboard_entries_count(leaderboard);
    for (size_t i = 0; i < count; ++i) {
        Dictionary entry;
        {
            uint64_t v {0};
            if (sb_leaderboard_entry_id(leaderboard, i, &v)) {
                entry["id"] = static_cast<int64_t>(v);
            }
        }
        {
            int v {0};
            if (sb_leaderboard_entry_rank(leaderboard, i, &v)) {
                entry["rank"] = v;
            }
        }
        {
            sb_score_t v {0};
            if (sb_leaderboard_entry_high_score(leaderboard, i, &v)) {
                entry["high_score"] = static_cast<int64_t>(v);
            }
        }
        {
            const char *v {nullptr};
            if (sb_leaderboard_entry_image(leaderboard, i, &v)) {
                entry["image"] = toString(v);
            }
        }
        {
            int v {0};
            if (sb_leaderboard_entry_reaction_count(leaderboard, i, &v)) {
                entry["reaction_count"] = v;
            }
        }
        {
            int v {0};
            if (sb_leaderboard_entry_score_count(leaderboard, i, &v)) {
                entry["score_count"] = v;
            }
        }
        {
            bool v {false};
            if (sb_leaderboard_entry_is_nfc_verified(leaderboard, i, &v)) {
                entry["is_nfc_verified"] = v;
            }
        }
        {
            bool v {false};
            if (sb_leaderboard_entry_is_verified(leaderboard, i, &v)) {
                entry["is_verified"] = v;
            }
        }
        {
            bool v {false};
            if (sb_leaderboard_entry_is_vpin(leaderboard, i, &v)) {
                entry["is_vpin"] = v;
            }
        }
        {
            const char *v {nullptr};
            if (sb_leaderboard_entry_created(leaderboard, i, &v)) {
                entry["created"] = toString(v);
            }
        }
        {
            const char *v {nullptr};
            if (sb_leaderboard_entry_player_id(leaderboard, i, &v)) {
                entry["player_id"] = toString(v);
            }
        }
        {
            const char *v {nullptr};
            if (sb_leaderboard_entry_player_username(leaderboard, i, &v)) {
                entry["player_username"] = toString(v);
            }
        }
        {
            const char *v {nullptr};
            if (sb_leaderboard_entry_player_display_name(leaderboard, i, &v)) {
                entry["player_display_name"] = toString(v);
            }
        }
        {
            const char *v {nullptr};
            if (sb_leaderboard_entry_player_initials(leaderboard, i, &v)) {
                entry["player_initials"] = toString(v);
            }
        }
        {
            const char *v {nullptr};
            if (sb_leaderboard_entry_player_avatar(leaderboard, i, &v)) {
                entry["player_avatar"] = toString(v);
            }
        }
        {
            int v {0};
            if (sb_leaderboard_entry_player_follower_count(leaderboard, i, &v)) {
                entry["player_follower_count"] = v;
            }
        }
        {
            int v {0};
            if (sb_leaderboard_entry_player_following_count(leaderboard, i, &v)) {
                entry["player_following_count"] = v;
            }
        }
        {
            const char *v {nullptr};
            if (sb_leaderboard_entry_player_last_login(leaderboard, i, &v)) {
                entry["player_last_login"] = toString(v);
            }
        }
        entries.push_back(entry);
    }
    return entries;
}

// Results of requests are queued like events, tagged with which request they answer.
void queue(Bridge &bridge, Dictionary result)
{
    std::scoped_lock lock(bridge.mutex);
    bridge.events.push_back(std::move(result));
}

Dictionary stringResult(const char *name, sb_error_t error, const char *reply)
{
    Dictionary result;
    result["_result"] = name;
    result["error"] = static_cast<int>(error);
    result["reply"] = toString(reply);
    return result;
}

void onPairCode(sb_error_t error, const char *reply, void *userData)
{
    queue(*static_cast<Bridge *>(userData), stringResult("pair_code", error, reply));
}

void onUnpair(sb_error_t error, const char *reply, void *userData)
{
    queue(*static_cast<Bridge *>(userData), stringResult("unpair", error, reply));
}

void onPairMachine(sb_error_t error, const char *reply, void *userData)
{
    queue(*static_cast<Bridge *>(userData), stringResult("pair_machine", error, reply));
}

void onTopScores(sb_error_t error, sb_leaderboard_t *leaderboard, void *userData)
{
    Dictionary result;
    result["_result"] = "top_scores";
    result["error"] = static_cast<int>(error);
    result["entries"] = leaderboard ? readLeaderboard(leaderboard) : Array();
    queue(*static_cast<Bridge *>(userData), std::move(result));
}

// A download's callback carries only its user data, so each one gets the URL its signal reports.
struct DownloadContext {
    std::shared_ptr<Bridge> bridge;
    String url;
};

void onDownload(sb_error_t error, const char *reply, void *userData)
{
    std::unique_ptr<DownloadContext> context(static_cast<DownloadContext *>(userData));
    Dictionary result = stringResult("download", error, reply);
    result["url"] = context->url;
    queue(*context->bridge, std::move(result));
}

void onDownloadBuffer(sb_error_t error, const uint8_t *data, size_t size, void *userData)
{
    std::unique_ptr<DownloadContext> context(static_cast<DownloadContext *>(userData));
    Dictionary result;
    result["url"] = context->url;
    result["_result"] = "download_buffer";
    result["error"] = static_cast<int>(error);
    PackedByteArray bytes;
    bytes.resize(static_cast<int64_t>(size));
    if (size > 0 && data) {
        std::memcpy(bytes.ptrw(), data, size);
    }
    result["data"] = bytes;
    queue(*context->bridge, std::move(result));
}

void onDeviceState(sb_error_t error, int httpStatus, const char *reply, void *userData)
{
    Dictionary result = stringResult("device_state", error, reply);
    result["http_status"] = httpStatus;
    queue(*static_cast<Bridge *>(userData), std::move(result));
}

// The SDK's logger is process-wide, so its messages wait here for whichever node drains them.
// Capped, so a burst of debug logging can't grow it without bound while nothing drains it.
constexpr size_t MAX_QUEUED_LOGS = 1000;

struct LogQueue {
    std::mutex mutex;
    std::vector<Dictionary> messages;
};

LogQueue &logQueue()
{
    static auto *instance = new LogQueue; // never freed: the SDK may log during shutdown
    return *instance;
}

void onLog(const char *message, sb_log_level_t level, const char *file, int line, int64_t timestamp,
           void * /*userData*/)
{
    Dictionary entry;
    entry["message"] = toString(message);
    entry["level"] = static_cast<int>(level);
    entry["file"] = toString(file);
    entry["line"] = line;
    entry["timestamp"] = timestamp;
    auto &q = logQueue();
    std::scoped_lock lock(q.mutex);
    if (q.messages.size() >= MAX_QUEUED_LOGS) {
        q.messages.erase(q.messages.begin());
    }
    q.messages.push_back(std::move(entry));
}

// The SDK opens files with the C++ standard library, which knows nothing of user:// or res://.
CharString toFsPath(const String &path)
{
    return ProjectSettings::get_singleton()->globalize_path(path).utf8();
}

std::vector<CharString> toFsPaths(const PackedStringArray &paths)
{
    std::vector<CharString> out;
    out.reserve(static_cast<size_t>(paths.size()));
    for (int64_t i = 0; i < paths.size(); ++i) {
        out.push_back(toFsPath(paths[i]));
    }
    return out;
}

std::vector<CharString> toUtf8(const PackedStringArray &strings)
{
    std::vector<CharString> out;
    out.reserve(static_cast<size_t>(strings.size()));
    for (int64_t i = 0; i < strings.size(); ++i) {
        out.push_back(strings[i].utf8());
    }
    return out;
}

std::vector<const char *> toPointers(const std::vector<CharString> &strings)
{
    std::vector<const char *> out;
    for (const auto &s : strings) {
        out.push_back(s.get_data());
    }
    return out;
}

// Header storage and the sb_http_header_t array pointing into it, valid while this lives.
struct Headers {
    std::vector<CharString> storage;
    std::vector<sb_http_header_t> array;
};

Headers toHeaders(const Dictionary &headers)
{
    Headers out;
    const Array keys = headers.keys();
    out.storage.reserve(static_cast<size_t>(keys.size()) * 2);
    for (int64_t i = 0; i < keys.size(); ++i) {
        out.storage.push_back(String(keys[i]).utf8());
        out.storage.push_back(String(headers[keys[i]]).utf8());
    }
    for (size_t i = 0; i + 1 < out.storage.size(); i += 2) {
        out.array.push_back({out.storage[i].get_data(), out.storage[i + 1].get_data()});
    }
    return out;
}

void onEvent(const sb_event_t *event, void *userData)
{
    auto *bridge = static_cast<Bridge *>(userData);
    auto data = readEvent(event);
    std::scoped_lock lock(bridge->mutex);
    bridge->events.push_back(std::move(data));
}

void onSaveKey(const char *key, void *userData)
{
    auto *bridge = static_cast<Bridge *>(userData);
    if (key && !saveKey(bridge->keyPath, key)) {
        // Without it the machine is a new device on the next run, and has to be paired again.
        UtilityFunctions::push_error(String("Scorbit: could not save the device key to ")
                                     + String::utf8(bridge->keyPath.c_str()));
    }
}

int onLoadKey(char *buffer, size_t bufferSize, void *userData)
{
    auto *bridge = static_cast<Bridge *>(userData);
    const auto key = loadKey(bridge->keyPath);
    if (!key) {
        return 0; // first run: no key yet
    }
    if (key->size() >= bufferSize) {
        return -1;
    }
    std::memcpy(buffer, key->data(), key->size());
    buffer[key->size()] = '\0';
    return static_cast<int>(key->size());
}

} // namespace

ScorbitGameState::ScorbitGameState()
    : m_bridge(std::make_shared<Bridge>())
{
}

ScorbitGameState::~ScorbitGameState()
{
    stop();
}

bool ScorbitGameState::start(const Dictionary &config)
{
    if (m_handle) {
        UtilityFunctions::push_warning("Scorbit: already started");
        return true;
    }
    // The C API needs all four; encrypted_key because the add-on has no hardware signer.
    String missing;
    for (const char *key : {"provider", "machine_id", "game_code_version", "encrypted_key"}) {
        if (!config.has(key)) {
            missing += missing.is_empty() ? String(key) : String(", ") + key;
        }
    }
    if (!missing.is_empty()) {
        UtilityFunctions::push_error("Scorbit: config is missing " + missing);
        return false;
    }

    const String keyPath = config.get("key_path", DEFAULT_KEY_PATH);
    m_bridge->keyPath = toFsPath(keyPath).get_data();

    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, String(config["provider"]).utf8().get_data());
    sb_config_set_machine_id(cfg, static_cast<int32_t>(int64_t(config["machine_id"])));
    sb_config_set_game_code_version(cfg, String(config["game_code_version"]).utf8().get_data());
    if (config.has("hostname")) {
        sb_config_set_hostname(cfg, String(config["hostname"]).utf8().get_data());
    }
    sb_config_set_encrypted_key(cfg, String(config["encrypted_key"]).utf8().get_data());
    if (config.has("score_features")) {
        const auto features = toUtf8(PackedStringArray(config["score_features"]));
        auto pointers = toPointers(features);
        sb_config_set_score_features(cfg, pointers.data(), pointers.size(),
                                     int(config.get("score_features_version", 1)));
    }
    if (config.has("threads_priority")) {
        sb_config_set_threads_priority(cfg, int(config["threads_priority"]));
    }
    if (config.has("worker_thread_count")) {
        sb_config_set_worker_thread_count(cfg, int(config["worker_thread_count"]));
    }
    if (config.has("machine_title")) {
        sb_config_set_machine_title(cfg, String(config["machine_title"]).utf8().get_data());
    }
    if (config.has("game_firmware")) {
        const Dictionary fw = config["game_firmware"];
        sb_config_set_game_firmware(cfg, String(fw.get("id", "")).utf8().get_data(),
                                    String(fw.get("version", "")).utf8().get_data(),
                                    String(fw.get("serial", "")).utf8().get_data());
    }
    if (config.has("uuid")) {
        sb_config_set_uuid(cfg, String(config["uuid"]).utf8().get_data());
    }
    if (config.has("serial_number")) {
        sb_config_set_serial_number(cfg, static_cast<uint64_t>(int64_t(config["serial_number"])));
    }
    if (config.has("extra_fingerprint")) {
        sb_config_set_extra_fingerprint(cfg, String(config["extra_fingerprint"]).utf8().get_data());
    }
    if (config.has("cf_hostname")) {
        sb_config_set_cf_hostname(cfg, String(config["cf_hostname"]).utf8().get_data());
    }
    if (config.has("heartbeat_host")) {
        sb_config_set_heartbeat_host(cfg, String(config["heartbeat_host"]).utf8().get_data());
    }
    if (config.has("heartbeat_port")) {
        sb_config_set_heartbeat_port(cfg, static_cast<uint16_t>(int(config["heartbeat_port"])));
    }
    if (config.has("auto_download_player_pics")) {
        sb_config_set_auto_download_player_pics(cfg, bool(config["auto_download_player_pics"]));
    }
    sb_config_set_event_callback(cfg, &onEvent, m_bridge.get());
    sb_config_set_save_key_callback(cfg, &onSaveKey, m_bridge.get());
    sb_config_set_load_key_callback(cfg, &onLoadKey, m_bridge.get());

    m_handle = sb_create_game_state(cfg);
    sb_config_destroy(cfg);
    if (!m_handle) {
        UtilityFunctions::push_error(
                "Scorbit: could not create the game state (is encrypted_key set?)");
        return false;
    }
    set_process(true);
    return true;
}

void ScorbitGameState::stop()
{
    if (!m_handle) {
        return;
    }
    // Stops the SDK's threads, so no callback reaches the bridge afterwards.
    sb_destroy_game_state(m_handle);
    m_handle = nullptr;
    set_process(false);
    // Events this game state raised but never delivered must not reach the next one.
    std::scoped_lock lock(m_bridge->mutex);
    m_bridge->events.clear();
}

bool ScorbitGameState::is_started() const
{
    return m_handle != nullptr;
}

void ScorbitGameState::_process(double /*delta*/)
{
    std::vector<Dictionary> events;
    {
        std::scoped_lock lock(m_bridge->mutex);
        events.swap(m_bridge->events);
    }
    for (const auto &event : events) {
        emitEvent(event);
    }
    if (m_logSignal) {
        std::vector<Dictionary> logs;
        {
            auto &q = logQueue();
            std::scoped_lock lock(q.mutex);
            logs.swap(q.messages);
        }
        for (const auto &log : logs) {
            emit_signal("log_message", log["message"], log["level"], log["file"], log["line"],
                        log["timestamp"]);
        }
    }
}

void ScorbitGameState::emitEvent(const Dictionary &event)
{
    if (event.has("_result")) {
        const String name = event["_result"];
        const int error = event["error"];
        if (name == "top_scores") {
            emit_signal("top_scores_received", error, event["entries"]);
        } else if (name == "pair_code") {
            emit_signal("pair_code_received", error, event["reply"]);
        } else if (name == "unpair") {
            emit_signal("unpair_finished", error, event["reply"]);
        } else if (name == "pair_machine") {
            emit_signal("pair_machine_finished", error, event["reply"]);
        } else if (name == "download") {
            emit_signal("download_finished", error, event["url"], event["reply"]);
        } else if (name == "download_buffer") {
            emit_signal("download_buffer_finished", error, event["url"], event["data"]);
        } else if (name == "device_state") {
            emit_signal("device_state_reported", error, event["http_status"], event["reply"]);
        }
        return;
    }
    const int type = event["type"];
    emit_signal("sdk_event", type, event);
    switch (type) {
    case SB_EVT_PAIRING_STATUS_CHANGED:
        emit_signal("pairing_status_changed", event.get("is_paired", false));
        break;
    case SB_EVT_GAME_START_REQUESTED:
        emit_signal("game_start_requested", event.get("players", 0));
        break;
    case SB_EVT_PLAYERS_UPDATED:
        emit_signal("players_updated", event.get("players", Dictionary()));
        break;
    case SB_EVT_CREDITS_ADD_REQUESTED:
        emit_signal("credits_add_requested", event.get("credits", 0), event.get("transaction", ""));
        break;
    case SB_EVT_CREDITS_STATUS_REQUESTED:
        emit_signal("credits_status_requested");
        break;
    case SB_EVT_PRICING_RECEIVED:
        emit_signal("pricing_received", event);
        break;
    case SB_EVT_DIAGNOSTICS_UPLOAD_REQUESTED:
        emit_signal("diagnostics_upload_requested", event.get("include_recordings", false));
        break;
    case SB_EVT_DIAGNOSTICS_UPLOADED:
        emit_signal("diagnostics_uploaded", event.get("success", false));
        break;
    case SB_EVT_PLAYER_PICTURE_READY:
        emit_signal("player_picture_ready", event.get("player", 0),
                    event.get("picture", PackedByteArray()));
        break;
    default:
        break;
    }
}

void ScorbitGameState::set_game_started(int origin)
{
    if (m_handle) {
        sb_set_game_started(m_handle, static_cast<sb_game_start_origin_t>(origin));
    }
}

void ScorbitGameState::set_game_finished()
{
    if (m_handle) {
        sb_set_game_finished(m_handle);
    }
}

void ScorbitGameState::set_current_ball(int ball)
{
    if (m_handle) {
        sb_set_current_ball(m_handle, static_cast<sb_ball_t>(ball));
    }
}

void ScorbitGameState::set_active_player(int player)
{
    if (m_handle) {
        sb_set_active_player(m_handle, static_cast<sb_player_t>(player));
    }
}

void ScorbitGameState::set_score(int player, int64_t score, int feature)
{
    if (m_handle) {
        sb_set_score(m_handle, static_cast<sb_player_t>(player), score, feature);
    }
}

void ScorbitGameState::add_mode(const String &mode)
{
    if (m_handle) {
        sb_add_mode(m_handle, mode.utf8().get_data());
    }
}

void ScorbitGameState::add_mode_expiring(const String &mode, int seconds)
{
    if (m_handle) {
        sb_add_mode_expiring(m_handle, mode.utf8().get_data(), static_cast<uint32_t>(seconds));
    }
}

void ScorbitGameState::remove_mode(const String &mode)
{
    if (m_handle) {
        sb_remove_mode(m_handle, mode.utf8().get_data());
    }
}

void ScorbitGameState::clear_modes()
{
    if (m_handle) {
        sb_clear_modes(m_handle);
    }
}

void ScorbitGameState::set_mode_completed(const String &mode)
{
    if (m_handle) {
        sb_set_mode_completed(m_handle, mode.utf8().get_data());
    }
}

void ScorbitGameState::set_capabilities(int capabilities)
{
    if (m_handle) {
        sb_set_capabilities(m_handle, static_cast<sb_capabilities_t>(capabilities));
    }
}

void ScorbitGameState::set_credits_dropped(int credits, const String &transaction, bool success)
{
    if (m_handle) {
        sb_set_credits_dropped(m_handle, credits, transaction.utf8().get_data(), success);
    }
}

void ScorbitGameState::set_credits_status(bool freePlay, int credits, int maxCredits,
                                          const String &pricing)
{
    if (m_handle) {
        sb_set_credits_status(m_handle, freePlay, credits, maxCredits, pricing.utf8().get_data());
    }
}

void ScorbitGameState::request_pair_code()
{
    if (m_handle) {
        sb_request_pair_code(m_handle, &onPairCode, m_bridge.get());
    }
}

void ScorbitGameState::request_unpair()
{
    if (m_handle) {
        sb_request_unpair(m_handle, &onUnpair, m_bridge.get());
    }
}

void ScorbitGameState::request_pair_machine(const String &machineUuid, const String &ownerUuid)
{
    if (m_handle) {
        sb_game_request_pair_machine(m_handle, machineUuid.utf8().get_data(),
                                     ownerUuid.utf8().get_data(), &onPairMachine, m_bridge.get());
    }
}

void ScorbitGameState::request_top_scores(int scope, int period, const String &since,
                                          int vpinFilter)
{
    if (m_handle) {
        const auto sinceUtf8 = since.utf8();
        sb_request_top_scores(m_handle, static_cast<sb_leaderboard_scope_t>(scope),
                              static_cast<sb_leaderboard_period_t>(period),
                              since.is_empty() ? nullptr : sinceUtf8.get_data(),
                              static_cast<sb_leaderboard_vpin_filter_t>(vpinFilter), &onTopScores,
                              m_bridge.get());
    }
}

void ScorbitGameState::download(const String &url, const String &filename,
                                const Dictionary &headers)
{
    if (m_handle) {
        const auto h = toHeaders(headers);
        sb_download(m_handle, url.utf8().get_data(), toFsPath(filename).get_data(), h.array.data(),
                    h.array.size(), &onDownload, new DownloadContext {m_bridge, url});
    }
}

void ScorbitGameState::download_buffer(const String &url, int reserveSize,
                                       const Dictionary &headers)
{
    if (reserveSize < 0) {
        UtilityFunctions::push_error(
                "Scorbit: download_buffer() reserve_size must not be negative");
        return;
    }
    if (m_handle) {
        const auto h = toHeaders(headers);
        sb_download_buffer(m_handle, url.utf8().get_data(), static_cast<size_t>(reserveSize),
                           h.array.data(), h.array.size(), &onDownloadBuffer,
                           new DownloadContext {m_bridge, url});
    }
}

void ScorbitGameState::upload_diagnostics(const PackedStringArray &logPaths,
                                          const PackedStringArray &recordingPaths,
                                          const String &logString, int64_t requestGeneration)
{
    if (m_handle) {
        const auto logs = toFsPaths(logPaths);
        const auto recordings = toFsPaths(recordingPaths);
        auto logPtrs = toPointers(logs);
        auto recordingPtrs = toPointers(recordings);
        const uint64_t generation = static_cast<uint64_t>(requestGeneration);
        sb_upload_diagnostics_ex(
                m_handle, logPtrs.empty() ? nullptr : logPtrs.data(), logPtrs.size(),
                recordingPtrs.empty() ? nullptr : recordingPtrs.data(), recordingPtrs.size(),
                logString.utf8().get_data(), requestGeneration < 0 ? nullptr : &generation);
    }
}

void ScorbitGameState::report_device_state(const String &type, const String &version,
                                           bool installed, const String &log)
{
    if (m_handle) {
        sb_report_device_state(m_handle, type.utf8().get_data(), version.utf8().get_data(),
                               installed, log.is_empty() ? nullptr : log.utf8().get_data(),
                               &onDeviceState, m_bridge.get());
    }
}

bool ScorbitGameState::logger_callbacks_supported() const
{
    return sb_logger_callbacks_supported();
}

void ScorbitGameState::enable_log_messages(int maxLength)
{
    if (maxLength < 1) {
        UtilityFunctions::push_error("Scorbit: enable_log_messages() max_length must be positive");
        return;
    }
    if (!m_logSignal) {
        sb_add_logger_callback(&onLog, nullptr, static_cast<size_t>(maxLength));
        m_logSignal = true;
    }
}

void ScorbitGameState::disable_log_messages()
{
    if (m_logSignal) {
        sb_reset_logger();
        m_logSignal = false;
    }
}

int64_t ScorbitGameState::get_machine_serial() const
{
    return m_handle ? static_cast<int64_t>(sb_get_machine_serial(m_handle)) : 0;
}

void ScorbitGameState::commit()
{
    if (m_handle) {
        sb_commit(m_handle);
    }
}

int ScorbitGameState::get_status() const
{
    return m_handle ? static_cast<int>(sb_get_status(m_handle)) : SB_NET_NOT_AUTHENTICATED;
}

String ScorbitGameState::get_machine_uuid() const
{
    return m_handle ? toString(sb_get_machine_uuid(m_handle)) : String();
}

String ScorbitGameState::get_pair_deeplink() const
{
    return m_handle ? toString(sb_get_pair_deeplink(m_handle)) : String();
}

void ScorbitGameState::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("start", "config"), &ScorbitGameState::start);
    ClassDB::bind_method(D_METHOD("stop"), &ScorbitGameState::stop);
    ClassDB::bind_method(D_METHOD("is_started"), &ScorbitGameState::is_started);
    ClassDB::bind_method(D_METHOD("set_game_started", "origin"),
                         &ScorbitGameState::set_game_started, DEFVAL(SB_GAME_STARTED_BY_BUTTON));
    ClassDB::bind_method(D_METHOD("set_game_finished"), &ScorbitGameState::set_game_finished);
    ClassDB::bind_method(D_METHOD("set_current_ball", "ball"), &ScorbitGameState::set_current_ball);
    ClassDB::bind_method(D_METHOD("set_active_player", "player"),
                         &ScorbitGameState::set_active_player);
    ClassDB::bind_method(D_METHOD("set_score", "player", "score", "feature"),
                         &ScorbitGameState::set_score, DEFVAL(0));
    ClassDB::bind_method(D_METHOD("add_mode", "mode"), &ScorbitGameState::add_mode);
    ClassDB::bind_method(D_METHOD("add_mode_expiring", "mode", "seconds"),
                         &ScorbitGameState::add_mode_expiring);
    ClassDB::bind_method(D_METHOD("remove_mode", "mode"), &ScorbitGameState::remove_mode);
    ClassDB::bind_method(D_METHOD("clear_modes"), &ScorbitGameState::clear_modes);
    ClassDB::bind_method(D_METHOD("set_mode_completed", "mode"),
                         &ScorbitGameState::set_mode_completed);
    ClassDB::bind_method(D_METHOD("set_capabilities", "capabilities"),
                         &ScorbitGameState::set_capabilities);
    ClassDB::bind_method(D_METHOD("set_credits_dropped", "credits", "transaction", "success"),
                         &ScorbitGameState::set_credits_dropped, DEFVAL(true));
    ClassDB::bind_method(
            D_METHOD("set_credits_status", "free_play", "credits", "max_credits", "pricing"),
            &ScorbitGameState::set_credits_status, DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("request_pair_code"), &ScorbitGameState::request_pair_code);
    ClassDB::bind_method(D_METHOD("request_unpair"), &ScorbitGameState::request_unpair);
    ClassDB::bind_method(D_METHOD("request_pair_machine", "machine_uuid", "owner_uuid"),
                         &ScorbitGameState::request_pair_machine);
    ClassDB::bind_method(D_METHOD("request_top_scores", "scope", "period", "since", "vpin_filter"),
                         &ScorbitGameState::request_top_scores,
                         DEFVAL(SB_LEADERBOARD_SCOPE_MACHINE),
                         DEFVAL(SB_LEADERBOARD_PERIOD_ALL_TIME), DEFVAL(String()),
                         DEFVAL(SB_LEADERBOARD_VPIN_ANY));
    ClassDB::bind_method(D_METHOD("get_machine_serial"), &ScorbitGameState::get_machine_serial);
    ClassDB::bind_method(D_METHOD("download", "url", "filename", "headers"),
                         &ScorbitGameState::download, DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("download_buffer", "url", "reserve_size", "headers"),
                         &ScorbitGameState::download_buffer, DEFVAL(0), DEFVAL(Dictionary()));
    ClassDB::bind_method(D_METHOD("upload_diagnostics", "log_paths", "recording_paths",
                                  "log_string", "request_generation"),
                         &ScorbitGameState::upload_diagnostics, DEFVAL(PackedStringArray()),
                         DEFVAL(PackedStringArray()), DEFVAL(String()), DEFVAL(-1));
    ClassDB::bind_method(D_METHOD("report_device_state", "type", "version", "installed", "log"),
                         &ScorbitGameState::report_device_state, DEFVAL(true), DEFVAL(String()));
    ClassDB::bind_method(D_METHOD("logger_callbacks_supported"),
                         &ScorbitGameState::logger_callbacks_supported);
    ClassDB::bind_method(D_METHOD("enable_log_messages", "max_length"),
                         &ScorbitGameState::enable_log_messages, DEFVAL(512));
    ClassDB::bind_method(D_METHOD("disable_log_messages"), &ScorbitGameState::disable_log_messages);
    ClassDB::bind_method(D_METHOD("commit"), &ScorbitGameState::commit);
    ClassDB::bind_method(D_METHOD("get_status"), &ScorbitGameState::get_status);
    ClassDB::bind_method(D_METHOD("get_machine_uuid"), &ScorbitGameState::get_machine_uuid);
    ClassDB::bind_method(D_METHOD("get_pair_deeplink"), &ScorbitGameState::get_pair_deeplink);

    ADD_SIGNAL(MethodInfo("sdk_event", PropertyInfo(Variant::INT, "type"),
                          PropertyInfo(Variant::DICTIONARY, "data")));
    ADD_SIGNAL(MethodInfo("pairing_status_changed", PropertyInfo(Variant::BOOL, "is_paired")));
    ADD_SIGNAL(MethodInfo("game_start_requested", PropertyInfo(Variant::INT, "players")));
    ADD_SIGNAL(MethodInfo("players_updated", PropertyInfo(Variant::DICTIONARY, "players")));
    ADD_SIGNAL(MethodInfo("credits_add_requested", PropertyInfo(Variant::INT, "credits"),
                          PropertyInfo(Variant::STRING, "transaction")));
    ADD_SIGNAL(MethodInfo("credits_status_requested"));
    ADD_SIGNAL(MethodInfo("top_scores_received", PropertyInfo(Variant::INT, "error"),
                          PropertyInfo(Variant::ARRAY, "entries")));
    ADD_SIGNAL(MethodInfo("download_finished", PropertyInfo(Variant::INT, "error"),
                          PropertyInfo(Variant::STRING, "url"),
                          PropertyInfo(Variant::STRING, "result")));
    ADD_SIGNAL(MethodInfo("download_buffer_finished", PropertyInfo(Variant::INT, "error"),
                          PropertyInfo(Variant::STRING, "url"),
                          PropertyInfo(Variant::PACKED_BYTE_ARRAY, "data")));
    ADD_SIGNAL(MethodInfo("device_state_reported", PropertyInfo(Variant::INT, "error"),
                          PropertyInfo(Variant::INT, "http_status"),
                          PropertyInfo(Variant::STRING, "reply")));
    ADD_SIGNAL(MethodInfo("diagnostics_upload_requested",
                          PropertyInfo(Variant::BOOL, "include_recordings")));
    ADD_SIGNAL(MethodInfo("diagnostics_uploaded", PropertyInfo(Variant::BOOL, "success")));
    ADD_SIGNAL(MethodInfo("log_message", PropertyInfo(Variant::STRING, "message"),
                          PropertyInfo(Variant::INT, "level"),
                          PropertyInfo(Variant::STRING, "file"), PropertyInfo(Variant::INT, "line"),
                          PropertyInfo(Variant::INT, "timestamp")));
    ADD_SIGNAL(MethodInfo("pair_code_received", PropertyInfo(Variant::INT, "error"),
                          PropertyInfo(Variant::STRING, "code")));
    ADD_SIGNAL(MethodInfo("unpair_finished", PropertyInfo(Variant::INT, "error"),
                          PropertyInfo(Variant::STRING, "reply")));
    ADD_SIGNAL(MethodInfo("pair_machine_finished", PropertyInfo(Variant::INT, "error"),
                          PropertyInfo(Variant::STRING, "reply")));
    ADD_SIGNAL(MethodInfo("pricing_received", PropertyInfo(Variant::DICTIONARY, "pricing")));
    ADD_SIGNAL(MethodInfo("player_picture_ready", PropertyInfo(Variant::INT, "player"),
                          PropertyInfo(Variant::PACKED_BYTE_ARRAY, "picture")));

    const auto cls = get_class_static();
    ClassDB::bind_integer_constant(cls, "GameStartOrigin", "STARTED_BY_BUTTON",
                                   SB_GAME_STARTED_BY_BUTTON);
    ClassDB::bind_integer_constant(cls, "GameStartOrigin", "STARTED_FROM_LOBBY",
                                   SB_GAME_STARTED_FROM_LOBBY);
    ClassDB::bind_integer_constant(cls, "Capability", "CAPABILITY_START_GAME",
                                   SB_CAPABILITY_START_GAME, true);
    ClassDB::bind_integer_constant(cls, "Capability", "CAPABILITY_CREDIT_DROP",
                                   SB_CAPABILITY_CREDIT_DROP, true);
    ClassDB::bind_integer_constant(cls, "LeaderboardScope", "LEADERBOARD_SCOPE_MACHINE",
                                   SB_LEADERBOARD_SCOPE_MACHINE);
    ClassDB::bind_integer_constant(cls, "LeaderboardScope", "LEADERBOARD_SCOPE_VARIANT",
                                   SB_LEADERBOARD_SCOPE_VARIANT);
    ClassDB::bind_integer_constant(cls, "LeaderboardScope", "LEADERBOARD_SCOPE_GAME",
                                   SB_LEADERBOARD_SCOPE_GAME);
    ClassDB::bind_integer_constant(cls, "LeaderboardPeriod", "LEADERBOARD_PERIOD_ALL_TIME",
                                   SB_LEADERBOARD_PERIOD_ALL_TIME);
    ClassDB::bind_integer_constant(cls, "LeaderboardPeriod", "LEADERBOARD_PERIOD_14D",
                                   SB_LEADERBOARD_PERIOD_14D);
    ClassDB::bind_integer_constant(cls, "LeaderboardPeriod", "LEADERBOARD_PERIOD_30D",
                                   SB_LEADERBOARD_PERIOD_30D);
    ClassDB::bind_integer_constant(cls, "LeaderboardPeriod", "LEADERBOARD_PERIOD_90D",
                                   SB_LEADERBOARD_PERIOD_90D);
    ClassDB::bind_integer_constant(cls, "LeaderboardPeriod", "LEADERBOARD_PERIOD_180D",
                                   SB_LEADERBOARD_PERIOD_180D);
    ClassDB::bind_integer_constant(cls, "LeaderboardPeriod", "LEADERBOARD_PERIOD_365D",
                                   SB_LEADERBOARD_PERIOD_365D);
    ClassDB::bind_integer_constant(cls, "LeaderboardVpinFilter", "LEADERBOARD_VPIN_ANY",
                                   SB_LEADERBOARD_VPIN_ANY);
    ClassDB::bind_integer_constant(cls, "LeaderboardVpinFilter", "LEADERBOARD_VPIN_ONLY",
                                   SB_LEADERBOARD_VPIN_ONLY);
    ClassDB::bind_integer_constant(cls, "LeaderboardVpinFilter", "LEADERBOARD_VPIN_REAL_ONLY",
                                   SB_LEADERBOARD_VPIN_REAL_ONLY);
    ClassDB::bind_integer_constant(cls, "Status", "NOT_AUTHENTICATED", SB_NET_NOT_AUTHENTICATED);
    ClassDB::bind_integer_constant(cls, "Status", "AUTHENTICATING", SB_NET_AUTHENTICATING);
    ClassDB::bind_integer_constant(cls, "Status", "CHECKING_PAIRING",
                                   SB_NET_AUTHENTICATED_CHECKING_PAIRING);
    ClassDB::bind_integer_constant(cls, "Status", "UNPAIRED", SB_NET_AUTHENTICATED_UNPAIRED);
    ClassDB::bind_integer_constant(cls, "Status", "PAIRED", SB_NET_AUTHENTICATED_PAIRED);
    ClassDB::bind_integer_constant(cls, "Status", "AUTHENTICATION_FAILED",
                                   SB_NET_AUTHENTICATION_FAILED);
    ClassDB::bind_integer_constant(cls, "Event", "EVENT_GAME_START_REQUESTED",
                                   SB_EVT_GAME_START_REQUESTED);
    ClassDB::bind_integer_constant(cls, "Event", "EVENT_CONFIG_RECEIVED", SB_EVT_CONFIG_RECEIVED);
    ClassDB::bind_integer_constant(cls, "Event", "EVENT_PLAYERS_UPDATED", SB_EVT_PLAYERS_UPDATED);
    ClassDB::bind_integer_constant(cls, "Event", "EVENT_PLAYER_PICTURE_READY",
                                   SB_EVT_PLAYER_PICTURE_READY);
    ClassDB::bind_integer_constant(cls, "Event", "EVENT_CREDITS_ADD_REQUESTED",
                                   SB_EVT_CREDITS_ADD_REQUESTED);
    ClassDB::bind_integer_constant(cls, "Event", "EVENT_CREDITS_STATUS_REQUESTED",
                                   SB_EVT_CREDITS_STATUS_REQUESTED);
    ClassDB::bind_integer_constant(cls, "Event", "EVENT_PRICING_RECEIVED", SB_EVT_PRICING_RECEIVED);
    ClassDB::bind_integer_constant(cls, "Event", "EVENT_PAIRING_STATUS_CHANGED",
                                   SB_EVT_PAIRING_STATUS_CHANGED);
}

} // namespace scorbit_godot
