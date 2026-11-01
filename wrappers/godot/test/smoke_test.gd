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


func _initialize() -> void:
	_expect_native = OS.get_cmdline_user_args() == PackedStringArray(["true"])
	_scorbit = load("res://addons/scorbit/scorbit.gd").new()
	root.add_child(_scorbit)
	# _ready runs once the main loop starts, so the session begins on the first frame.


func _process(_delta: float) -> bool:
	_frame += 1
	if _frame == 1:
		_begin()
	elif _frame == FRAMES / 2:
		_scorbit.set_score(1, 2500)
		_scorbit.set_game_finished()
		_scorbit.commit()
	if _frame < FRAMES or (_expect_logs and not _reported("missing.log") and _frame < MAX_FRAMES):
		return false

	# The SDK logs as it starts, so a working log bridge has delivered lines by now.
	_check((_logs > 0) == _expect_logs, "log_message delivered SDK log lines")
	if _expect_logs:
		# A user:// path must reach the SDK as a real path: only the missing file is skipped.
		_check(_reported("missing.log"), "upload_diagnostics() checked the log paths")
		_check(not _reported("diag.log"), "upload_diagnostics() found a user:// log file")
	_scorbit.disable_log_messages()
	_scorbit.stop()
	_check(not _scorbit.is_started(), "is_started() after stop()")
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
	_scorbit.request_top_scores(_scorbit.LEADERBOARD_SCOPE_MACHINE, _scorbit.LEADERBOARD_PERIOD_30D)
	_scorbit.request_pair_code()
	# Native builds may use either logger backend; only the callback one delivers log lines.
	_expect_logs = _expect_native and _scorbit.logger_callbacks_supported()
	if not _expect_native:
		_check(not _scorbit.logger_callbacks_supported(), "logger_callbacks_supported()")
	_scorbit.enable_log_messages()
	_scorbit.log_message.connect(_on_log)
	_scorbit.report_device_state("smoke", "1.0.0")
	DirAccess.make_dir_recursive_absolute("user://smoke")
	FileAccess.open("user://smoke/diag.log", FileAccess.WRITE).store_string("smoke log\n")
	_scorbit.upload_diagnostics(
			PackedStringArray(["user://smoke/diag.log", "user://smoke/missing.log"]),
			PackedStringArray(), "smoke diagnostics")
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


func _reported(file_name: String) -> bool:
	for line in _skipped:
		if line.ends_with("/" + file_name) or line.ends_with("\\" + file_name):
			return true
	return false


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failures += 1
		printerr("smoke_test FAILED: ", what)
