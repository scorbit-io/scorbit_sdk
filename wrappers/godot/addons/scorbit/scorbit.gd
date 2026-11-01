# Scorbit SDK for Godot 4: the API game code calls, as the Scorbit autoload.
#
# Every call forwards to the native ScorbitGameState. Without the native library (an unsupported
# platform, or the add-on's bin/ folder missing) is_available() is false and every call does
# nothing, so the game still runs.
extends Node

signal sdk_event(type: int, data: Dictionary)
signal pairing_status_changed(is_paired: bool)
signal game_start_requested(players: int)
signal players_updated(players: Dictionary)
signal player_picture_ready(player: int, picture: PackedByteArray)

const NATIVE_CLASS := "ScorbitGameState"

# Game start origins, for set_game_started().
const STARTED_BY_BUTTON := 0
const STARTED_FROM_LOBBY := 1

# get_status() values.
const NOT_AUTHENTICATED := 0
const AUTHENTICATING := 1
const CHECKING_PAIRING := 2
const UNPAIRED := 3
const PAIRED := 4
const AUTHENTICATION_FAILED := 5

var _native = null  # a ScorbitGameState; untyped, as the class exists only when the library loads


func _ready() -> void:
	if not ClassDB.class_exists(NATIVE_CLASS):
		push_warning("Scorbit: native library not loaded on this platform; Scorbit calls do nothing")
		return
	_native = ClassDB.instantiate(NATIVE_CLASS)
	_native.name = "ScorbitGameState"
	add_child(_native)
	# Re-emit the native signals as this node's own.
	_native.sdk_event.connect(sdk_event.emit)
	_native.pairing_status_changed.connect(pairing_status_changed.emit)
	_native.game_start_requested.connect(game_start_requested.emit)
	_native.players_updated.connect(players_updated.emit)
	_native.player_picture_ready.connect(player_picture_ready.emit)


func _exit_tree() -> void:
	stop()


## Whether the native SDK is loaded. When false, every other call does nothing.
func is_available() -> bool:
	return _native != null


## Creates the game state. config keys: provider and machine_id (required), game_code_version,
## encrypted_key, hostname ("production" by default), auto_download_player_pics, and key_path
## (where the device key is kept, user://scorbit/device_key by default).
func start(config: Dictionary) -> bool:
	return _native.start(config) if _native else false


func stop() -> void:
	if _native:
		_native.stop()


func is_started() -> bool:
	return _native.is_started() if _native else false


func set_game_started(origin: int = STARTED_BY_BUTTON) -> void:
	if _native:
		_native.set_game_started(origin)


func set_game_finished() -> void:
	if _native:
		_native.set_game_finished()


func set_current_ball(ball: int) -> void:
	if _native:
		_native.set_current_ball(ball)


func set_active_player(player: int) -> void:
	if _native:
		_native.set_active_player(player)


func set_score(player: int, score: int, feature: int = 0) -> void:
	if _native:
		_native.set_score(player, score, feature)


func add_mode(mode: String) -> void:
	if _native:
		_native.add_mode(mode)


func add_mode_expiring(mode: String, seconds: int) -> void:
	if _native:
		_native.add_mode_expiring(mode, seconds)


func remove_mode(mode: String) -> void:
	if _native:
		_native.remove_mode(mode)


func clear_modes() -> void:
	if _native:
		_native.clear_modes()


## Reports that the player beat a mode, as opposed to merely being in it.
func set_mode_completed(mode: String) -> void:
	if _native:
		_native.set_mode_completed(mode)


## Sends the changes since the last commit. Call once per frame or after a batch of changes.
func commit() -> void:
	if _native:
		_native.commit()


func get_status() -> int:
	return _native.get_status() if _native else NOT_AUTHENTICATED


func get_machine_uuid() -> String:
	return _native.get_machine_uuid() if _native else ""


func get_pair_deeplink() -> String:
	return _native.get_pair_deeplink() if _native else ""

