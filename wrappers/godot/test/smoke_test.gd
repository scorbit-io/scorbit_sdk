# Headless check of the Scorbit add-on: run by run_smoke.cmake as
#   godot --headless --path <project> -s res://smoke_test.gd -- <true|false>
# The argument says whether the native library is expected to load.
extends SceneTree

const FRAMES := 30
const MAX_FRAMES := 600  # log lines arrive asynchronously; give the SDK's worker time to report

var _expect_native := false
var _expect_logs := false  # a native build with the callback logger backend
var _scorbit: Node
var _frame := 0
var _failures := 0
var _logs := 0
var _skipped: PackedStringArray  # the SDK's "file does not exist, skipping" log lines
var _upload_starts := 0  # three uploads, so a single logger registration delivers this line 3x
var _listener  # a second native logging node, with its own, shorter line limit
var _listener_lines: PackedStringArray
var _uploads: Array[int] = []  # ids of the chained uploads, in request order
var _answered: Array[int] = []  # diagnostics_uploaded ids, in arrival order
var _quitter  # a logging node whose handler turns logging off on its first line
var _quitter_lines := 0


func _initialize() -> void:
	_expect_native = OS.get_cmdline_user_args() == PackedStringArray(["true"])
	_scorbit = load("res://addons/scorbit/scorbit.gd").new()
	root.add_child(_scorbit)
	# _ready runs once the main loop starts, so the session begins on the first frame.


func _process(_delta: float) -> bool:
	_frame += 1
	if _frame > MAX_FRAMES + 60:  # a script error below would otherwise keep the test running forever
		printerr("smoke_test FAILED: did not finish")
		quit(1)
		return true
	if _frame == 1:
		_begin()
	elif _frame == FRAMES / 2:
		_scorbit.set_score(1, 2500)
		_scorbit.set_game_finished()
		_scorbit.commit()
	var waiting := _expect_native and _answered.size() < 2
	waiting = waiting or (_expect_logs and not _reported("missing.log"))
	if _frame < FRAMES or (waiting and _frame < MAX_FRAMES):
		return false

	# The SDK logs as it starts, so a working log bridge has delivered lines by now.
	_check((_logs > 0) == _expect_logs, "log_message delivered SDK log lines")
	if _expect_logs:
		# A user:// path must reach the SDK as a real path: only the missing file is skipped.
		_check(_reported("missing.log"), "upload_diagnostics() checked the log paths")
		_check(not _reported("diag.log"), "upload_diagnostics() found a user:// log file")
		# Three uploads, each sent once, even though two were requested from the previous answer.
		_check(_upload_starts == 3, "each upload sent once; log lines delivered once")
		_check(not _listener_lines.is_empty(), "a second logging node got log lines too")
		var cut := false
		for line in _listener_lines:
			_check(line.length() <= 20, "a node's own max_length applies: " + line)
			cut = cut or " ... " in line
		_check(cut, "long lines are cut for the node with the shorter limit")
		_check(_quitter_lines == 1, "disable_log_messages() from a handler ends the batch")
	if _expect_native:
		_check(_answered.size() >= 2 and _answered.slice(0, 2) == _uploads.slice(0, 2),
				"diagnostics_uploaded answers uploads in order")
		_check(not _answered.has(0), "no diagnostics_uploaded without a request")
	if _listener:
		_listener.free()
	if _quitter:
		_quitter.free()
	_scorbit.disable_log_messages()
	_scorbit.stop()
	_check(not _scorbit.is_started(), "is_started() after stop()")
	if _expect_native:
		_check(_scorbit._native._pending_request_count() == 0, "stop() frees unanswered requests")
	print("smoke_test: %d failure(s)" % _failures)
	quit(1 if _failures else 0)
	return true


func _begin() -> void:
	_check(ClassDB.class_exists("ScorbitGameState") == _expect_native, "native class loaded")
	_check(_scorbit.is_available() == _expect_native, "is_available()")

	_check(not _scorbit.start({"provider": "testprovider", "machine_id": 1234}),
			"start() refuses a config without game_code_version and encrypted_key")
	_check(not _scorbit.is_started(), "is_started() after a refused start()")

	# A dummy key and the staging host, as the SDK's own interface tests: no network needed.
	var started: bool = _scorbit.start({
		"provider": "testprovider",
		"machine_id": 1234,
		"game_code_version": "1.0.0",
		"hostname": "staging",
		"encrypted_key": "dummy_encrypted_key",
		"key_path": "user://smoke/device_key",
	})
	_check(started == _expect_native, "start()")
	_check(_scorbit.is_started() == _expect_native, "is_started() after start()")

	if _expect_native:
		_check(_scorbit.CAPABILITY_START_GAME == ClassDB.class_get_integer_constant(
				"ScorbitGameState", "CAPABILITY_START_GAME"), "CAPABILITY_START_GAME matches")
		_check(_scorbit.CAPABILITY_CREDIT_DROP == ClassDB.class_get_integer_constant(
				"ScorbitGameState", "CAPABILITY_CREDIT_DROP"), "CAPABILITY_CREDIT_DROP matches")
	_scorbit.set_capabilities(_scorbit.CAPABILITY_START_GAME | _scorbit.CAPABILITY_CREDIT_DROP)
	_scorbit.set_credits_status(false, 3, 10)
	_scorbit.set_credits_dropped(1, "smoke-transaction")
	if _expect_native:
		for name in ["LEADERBOARD_SCOPE_GAME", "LEADERBOARD_PERIOD_365D", "LEADERBOARD_VPIN_REAL_ONLY"]:
			var gd_value = _scorbit.get_script().get_script_constant_map().get(name)
			_check(gd_value == ClassDB.class_get_integer_constant("ScorbitGameState", name),
					name + " matches")
	var top_id: int = _scorbit.request_top_scores(_scorbit.LEADERBOARD_SCOPE_MACHINE,
			_scorbit.LEADERBOARD_PERIOD_30D)
	var pair_id: int = _scorbit.request_pair_code()
	if _expect_native:
		_check(top_id > 0 and pair_id > 0 and top_id != pair_id, "requests return distinct ids")
	else:
		_check(top_id == 0 and pair_id == 0, "requests without the native library return 0")
	# Native builds may use either logger backend; only the callback one delivers log lines.
	_expect_logs = _expect_native and _scorbit.logger_callbacks_supported()
	if not _expect_native:
		_check(not _scorbit.logger_callbacks_supported(), "logger_callbacks_supported()")
	_scorbit.enable_log_messages()
	_scorbit.log_message.connect(_on_log)
	if _expect_native:
		for i in 3:  # nodes that enable logging and go away must not leave a registration behind
			var extra: Object = ClassDB.instantiate("ScorbitGameState")
			extra.enable_log_messages(512)
			extra.free()
		_check(_has_arg("download_finished", "path"), "download_finished reports the saved path")
		_check(_has_arg("device_state_reported", "type"), "device_state_reported names the type")
		for name in ["top_scores_received", "pair_code_received", "unpair_finished",
				"pair_machine_finished", "download_finished", "download_buffer_finished",
				"device_state_reported", "diagnostics_uploaded"]:
			var args: Array = ClassDB.class_get_signal("ScorbitGameState", name).get("args", [])
			_check(not args.is_empty() and args[0].name == "request_id", name + " leads with request_id")
		_listener = ClassDB.instantiate("ScorbitGameState")
		root.add_child(_listener)
		_listener.log_message.connect(_on_listener_log)
		_listener.enable_log_messages(20)
		# Enabled now, so several lines are queued by its first frame.
		_quitter = ClassDB.instantiate("ScorbitGameState")
		root.add_child(_quitter)
		_quitter.log_message.connect(_on_quitter_log)
		_quitter.enable_log_messages(512)
	_scorbit.report_device_state("smoke", "1.0.0")
	DirAccess.make_dir_recursive_absolute("user://smoke")
	FileAccess.open("user://smoke/diag.log", FileAccess.WRITE).store_string("smoke log\n")
	# An upload with nothing in it is answered at once, with no network: chain three of them,
	# each requested from the previous one's answer, ending with real user:// files.
	_scorbit.diagnostics_uploaded.connect(_on_uploaded)
	_uploads.append(_scorbit.upload_diagnostics())
	_check(_scorbit.get_machine_serial() >= 0, "get_machine_serial()")

	_scorbit.set_game_started()
	_scorbit.set_active_player(1)
	_scorbit.set_current_ball(1)
	_scorbit.set_score(1, 1000)
	_scorbit.add_mode("MB:Multiball")
	_scorbit.set_mode_completed("MB:Multiball")
	_scorbit.remove_mode("MB:Multiball")
	_scorbit.commit()


func _on_log(message: String, _level: int, _file: String, _line: int, _timestamp: int) -> void:
	_logs += 1
	if "does not exist" in message:
		_skipped.append(message)
	elif "diagnostics upload: starting" in message:
		_upload_starts += 1


func _on_listener_log(message: String, _level: int, _file: String, _line: int, _time: int) -> void:
	_listener_lines.append(message)


func _on_quitter_log(_message: String, _level: int, _file: String, _line: int, _time: int) -> void:
	_quitter_lines += 1
	_quitter.disable_log_messages()


func _on_uploaded(request_id: int, _success: bool) -> void:
	_answered.append(request_id)
	if _answered.size() == 1:
		_uploads.append(_scorbit.upload_diagnostics())
	elif _answered.size() == 2:
		_uploads.append(_scorbit.upload_diagnostics(
				PackedStringArray(["user://smoke/diag.log", "user://smoke/missing.log"]),
				PackedStringArray(), "smoke diagnostics"))


func _reported(file_name: String) -> bool:
	for line in _skipped:
		if line.ends_with("/" + file_name) or line.ends_with("\\" + file_name):
			return true
	return false


func _has_arg(signal_name: String, arg: String) -> bool:
	for a in ClassDB.class_get_signal("ScorbitGameState", signal_name).get("args", []):
		if a.name == arg:
			return true
	return false


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failures += 1
		printerr("smoke_test FAILED: ", what)
