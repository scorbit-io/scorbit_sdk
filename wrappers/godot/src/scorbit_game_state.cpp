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
    if (key) {
        saveKey(bridge->keyPath, key);
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
    m_bridge->keyPath = ProjectSettings::get_singleton()->globalize_path(keyPath).utf8().get_data();

    sb_config_t cfg = sb_config_create();
    sb_config_set_provider(cfg, String(config["provider"]).utf8().get_data());
    sb_config_set_machine_id(cfg, static_cast<int32_t>(int64_t(config["machine_id"])));
    sb_config_set_game_code_version(cfg, String(config["game_code_version"]).utf8().get_data());
    if (config.has("hostname")) {
        sb_config_set_hostname(cfg, String(config["hostname"]).utf8().get_data());
    }
    sb_config_set_encrypted_key(cfg, String(config["encrypted_key"]).utf8().get_data());
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
}

void ScorbitGameState::emitEvent(const Dictionary &event)
{
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
    ClassDB::bind_method(D_METHOD("commit"), &ScorbitGameState::commit);
    ClassDB::bind_method(D_METHOD("get_status"), &ScorbitGameState::get_status);
    ClassDB::bind_method(D_METHOD("get_machine_uuid"), &ScorbitGameState::get_machine_uuid);
    ClassDB::bind_method(D_METHOD("get_pair_deeplink"), &ScorbitGameState::get_pair_deeplink);

    ADD_SIGNAL(MethodInfo("sdk_event", PropertyInfo(Variant::INT, "type"),
                          PropertyInfo(Variant::DICTIONARY, "data")));
    ADD_SIGNAL(MethodInfo("pairing_status_changed", PropertyInfo(Variant::BOOL, "is_paired")));
    ADD_SIGNAL(MethodInfo("game_start_requested", PropertyInfo(Variant::INT, "players")));
    ADD_SIGNAL(MethodInfo("players_updated", PropertyInfo(Variant::DICTIONARY, "players")));
    ADD_SIGNAL(MethodInfo("player_picture_ready", PropertyInfo(Variant::INT, "player"),
                          PropertyInfo(Variant::PACKED_BYTE_ARRAY, "picture")));

    const auto cls = get_class_static();
    ClassDB::bind_integer_constant(cls, "GameStartOrigin", "STARTED_BY_BUTTON",
                                   SB_GAME_STARTED_BY_BUTTON);
    ClassDB::bind_integer_constant(cls, "GameStartOrigin", "STARTED_FROM_LOBBY",
                                   SB_GAME_STARTED_FROM_LOBBY);
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
    ClassDB::bind_integer_constant(cls, "Event", "EVENT_PAIRING_STATUS_CHANGED",
                                   SB_EVT_PAIRING_STATUS_CHANGED);
}

} // namespace scorbit_godot
