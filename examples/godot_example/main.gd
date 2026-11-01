# A minimal game loop against the staging environment: one player, three balls, a mode.
# The staging provider and key the other examples use; use your own from Scorbit support.
extends Node

const CONFIG := {
	"provider": "dilshodpinball",
	"machine_id": 4379,
	"game_code_version": "0.1.0",
	"hostname": "staging",
	"encrypted_key": "8qWNpMPeO1AbgcoPSsdeUORGmO/hyB70oyrpFyRlYWbaVx4Kuan0CAGaXZWS3JWdgmPL7p9k3UFTwAp5y16L8O1tYaHLGkW4p/yWmA==",
}

var _score := 0
var _ball := 1
var _elapsed := 0.0


func _ready() -> void:
	if not Scorbit.is_available():
		print("Scorbit native library not loaded; the game runs without it")
		return
	Scorbit.pairing_status_changed.connect(func(is_paired): print("Paired: ", is_paired))
	Scorbit.players_updated.connect(_on_players_updated)
	if not Scorbit.start(CONFIG):
		push_error("Scorbit failed to start")
		return
	print("Pair this machine: ", Scorbit.get_pair_deeplink())
	Scorbit.set_game_started()
	Scorbit.set_active_player(1)
	Scorbit.set_current_ball(_ball)
	Scorbit.add_mode("MB:Multiball")


func _process(delta: float) -> void:
	if not Scorbit.is_started():
		return
	_elapsed += delta
	_score += 10
	Scorbit.set_score(1, _score)
	if _elapsed > 10.0:
		_elapsed = 0.0
		_ball += 1
		if _ball == 2:
			Scorbit.set_mode_completed("MB:Multiball")
			Scorbit.remove_mode("MB:Multiball")
		if _ball > 3:
			Scorbit.set_game_finished()
			Scorbit.commit()
			Scorbit.stop()
			return
		Scorbit.set_current_ball(_ball)
	Scorbit.commit()


func _on_players_updated(players: Dictionary) -> void:
	for number in players:
		var info: Dictionary = players[number]
		if info.get("has_info", false):
			print("Player %d: %s" % [number, info.get("preferred_name", "")])
		else:
			print("Player %d can claim at %s" % [number, info.get("claim_deeplink", "")])
