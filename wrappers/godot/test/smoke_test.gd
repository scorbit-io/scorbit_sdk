# Headless check of the Scorbit add-on: run by run_smoke.cmake as
#   godot --headless --path <project> -s res://smoke_test.gd -- <true|false>
# The argument says whether the native library is expected to load.
extends SceneTree

const FRAMES := 30

var _expect_native := false
var _scorbit: Node
var _frame := 0
var _failures := 0


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
	if _frame < FRAMES:
		return false

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

	_scorbit.set_game_started()
	_scorbit.set_active_player(1)
	_scorbit.set_current_ball(1)
	_scorbit.set_score(1, 1000)
	_scorbit.add_mode("MB:Multiball")
	_scorbit.set_mode_completed("MB:Multiball")
	_scorbit.remove_mode("MB:Multiball")
	_scorbit.commit()


func _check(ok: bool, what: String) -> void:
	if not ok:
		_failures += 1
		printerr("smoke_test FAILED: ", what)
