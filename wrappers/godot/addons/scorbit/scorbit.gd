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
## The Scorbit app asked to add credits. Answer with set_credits_dropped(credits, transaction).
signal credits_add_requested(credits: int, transaction: String)
## Scorbit asked for the machine's credit status. Answer with set_credits_status().
signal credits_status_requested()
## The machine's pricing: free_play, payments_enabled, credit_price, credit_regular_price,
## credit_sale_price, and bundles (each with credits, price, regular_price, sale_price).
signal pricing_received(pricing: Dictionary)
## Answers request_top_scores(): error 0 on success, and one Dictionary per entry (rank, high_score,
## player_display_name, player_initials, player_avatar, is_verified, created, ...).
signal top_scores_received(error: int, entries: Array)
## Answers download(): result is the saved file's path on success.
signal download_finished(error: int, url: String, result: String)
## Answers download_buffer() with the downloaded bytes.
signal download_buffer_finished(error: int, url: String, data: PackedByteArray)
## Answers report_device_state().
signal device_state_reported(error: int, http_status: int, reply: String)
## Scorbit support asked for diagnostics. Answer with upload_diagnostics().
signal diagnostics_upload_requested(include_recordings: bool)
## The diagnostics upload finished.
signal diagnostics_uploaded(success: bool)
## An SDK log line, after enable_log_messages(). level is 0 debug, 1 info, 2 warning, 3 error.
signal log_message(message: String, level: int, file: String, line: int, timestamp: int)
## Answers request_pair_code(): the short code a player enters in the Scorbit app.
signal pair_code_received(error: int, code: String)
## Answers request_unpair().
signal unpair_finished(error: int, reply: String)
## Answers request_pair_machine().
signal pair_machine_finished(error: int, reply: String)

const NATIVE_CLASS := "ScorbitGameState"

# Game start origins, for set_game_started().
const STARTED_BY_BUTTON := 0
const STARTED_FROM_LOBBY := 1

# Capability flags for set_capabilities(); combine with |.
const CAPABILITY_START_GAME := 1  # the Scorbit app can start a game (game_start_requested)
const CAPABILITY_CREDIT_DROP := 2  # the Scorbit app can add credits (credits_add_requested)

# request_top_scores() options.
const LEADERBOARD_SCOPE_MACHINE := 0
const LEADERBOARD_SCOPE_VARIANT := 1
const LEADERBOARD_SCOPE_GAME := 2
const LEADERBOARD_PERIOD_ALL_TIME := 0
const LEADERBOARD_PERIOD_14D := 1
const LEADERBOARD_PERIOD_30D := 2
const LEADERBOARD_PERIOD_90D := 3
const LEADERBOARD_PERIOD_180D := 4
const LEADERBOARD_PERIOD_365D := 5
const LEADERBOARD_VPIN_ANY := 0
const LEADERBOARD_VPIN_ONLY := 1
const LEADERBOARD_VPIN_REAL_ONLY := 2

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
	_native.credits_add_requested.connect(credits_add_requested.emit)
	_native.credits_status_requested.connect(credits_status_requested.emit)
	_native.pricing_received.connect(pricing_received.emit)
	_native.top_scores_received.connect(top_scores_received.emit)
	_native.pair_code_received.connect(pair_code_received.emit)
	_native.unpair_finished.connect(unpair_finished.emit)
	_native.pair_machine_finished.connect(pair_machine_finished.emit)
	_native.download_finished.connect(download_finished.emit)
	_native.download_buffer_finished.connect(download_buffer_finished.emit)
	_native.device_state_reported.connect(device_state_reported.emit)
	_native.diagnostics_upload_requested.connect(diagnostics_upload_requested.emit)
	_native.diagnostics_uploaded.connect(diagnostics_uploaded.emit)
	_native.log_message.connect(log_message.emit)


func _exit_tree() -> void:
	stop()


## Whether the native SDK is loaded. When false, every other call does nothing.
func is_available() -> bool:
	return _native != null


## Creates the game state. config keys: provider, machine_id, game_code_version and encrypted_key
## (required), hostname ("production" by default), auto_download_player_pics, key_path (where the
## device key is kept, user://scorbit/device_key by default), score_features (an Array of names) and
## score_features_version, machine_title, game_firmware ({id, version, serial}), uuid,
## serial_number, extra_fingerprint, threads_priority, worker_thread_count, cf_hostname,
## heartbeat_host and heartbeat_port.
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


## What the machine supports, e.g. CAPABILITY_START_GAME | CAPABILITY_CREDIT_DROP. Call after
## start(); until then no capability is advertised, so the Scorbit app can't start a game or add
## credits.
func set_capabilities(capabilities: int) -> void:
	if _native:
		_native.set_capabilities(capabilities)


## Reports credits added on a credits_add_requested, with the transaction it carried.
func set_credits_dropped(credits: int, transaction: String, success: bool = true) -> void:
	if _native:
		_native.set_credits_dropped(credits, transaction, success)


## Reports the machine's credit status, e.g. on credits_status_requested.
func set_credits_status(free_play: bool, credits: int, max_credits: int, pricing: String = "") -> void:
	if _native:
		_native.set_credits_status(free_play, credits, max_credits, pricing)


## Requests a leaderboard; the answer arrives as top_scores_received. since is an ISO-8601 date
## to count scores from, or "" for none.
func request_top_scores(scope: int = LEADERBOARD_SCOPE_MACHINE,
		period: int = LEADERBOARD_PERIOD_ALL_TIME, since: String = "",
		vpin_filter: int = LEADERBOARD_VPIN_ANY) -> void:
	if _native:
		_native.request_top_scores(scope, period, since, vpin_filter)


## Requests a short pairing code; the answer arrives as pair_code_received.
func request_pair_code() -> void:
	if _native:
		_native.request_pair_code()


## Unpairs the machine; the answer arrives as unpair_finished.
func request_unpair() -> void:
	if _native:
		_native.request_unpair()


## Pairs this machine to a venue machine for an owner; the answer arrives as pair_machine_finished.
func request_pair_machine(machine_uuid: String, owner_uuid: String) -> void:
	if _native:
		_native.request_pair_machine(machine_uuid, owner_uuid)


## Downloads url to filename; the answer arrives as download_finished. headers: {name: value}.
func download(url: String, filename: String, headers: Dictionary = {}) -> void:
	if _native:
		_native.download(url, filename, headers)


## Downloads url into memory; the answer arrives as download_buffer_finished.
func download_buffer(url: String, reserve_size: int = 0, headers: Dictionary = {}) -> void:
	if _native:
		_native.download_buffer(url, reserve_size, headers)


## Uploads diagnostics, e.g. on diagnostics_upload_requested. request_generation echoes the
## request it answers; -1 for none.
func upload_diagnostics(log_paths: PackedStringArray = PackedStringArray(),
		recording_paths: PackedStringArray = PackedStringArray(), log_string: String = "",
		request_generation: int = -1) -> void:
	if _native:
		_native.upload_diagnostics(log_paths, recording_paths, log_string, request_generation)


## Reports a typed piece of the device's state; the answer arrives as device_state_reported.
func report_device_state(type: String, version: String, installed: bool = true,
		log: String = "") -> void:
	if _native:
		_native.report_device_state(type, version, installed, log)


## Whether the native library can deliver its log lines (log_message).
func logger_callbacks_supported() -> bool:
	return _native.logger_callbacks_supported() if _native else false


## Starts emitting SDK log lines as log_message; messages longer than max_length are cut.
func enable_log_messages(max_length: int = 512) -> void:
	if _native:
		_native.enable_log_messages(max_length)


func disable_log_messages() -> void:
	if _native:
		_native.disable_log_messages()


func get_machine_serial() -> int:
	return _native.get_machine_serial() if _native else 0


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

