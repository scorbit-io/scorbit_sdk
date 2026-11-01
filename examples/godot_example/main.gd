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
var _pair_link_shown := false
var _credits := 0


func _ready() -> void:
	if not Scorbit.is_available():
		print("Scorbit native library not loaded; the game runs without it")
		return
	Scorbit.pairing_status_changed.connect(func(is_paired): print("Paired: ", is_paired))
	Scorbit.players_updated.connect(_on_players_updated)
	Scorbit.game_start_requested.connect(_on_game_start_requested)
	Scorbit.credits_add_requested.connect(_on_credits_add_requested)
	Scorbit.credits_status_requested.connect(_report_credits)
	if not Scorbit.start(CONFIG):
		push_error("Scorbit failed to start")
		return
	# Let the Scorbit app start games and add credits on this machine.
	Scorbit.set_capabilities(Scorbit.CAPABILITY_START_GAME | Scorbit.CAPABILITY_CREDIT_DROP)
	_report_credits()
	_start_game(Scorbit.STARTED_BY_BUTTON)


func _start_game(origin: int) -> void:
	_score = 0
	_ball = 1
	_elapsed = 0.0
	Scorbit.set_game_started(origin)
	Scorbit.set_active_player(1)
	Scorbit.set_current_ball(_ball)
	Scorbit.add_mode("MB:Multiball")
	set_process(true)


func _process(delta: float) -> void:
	if not Scorbit.is_started():
		return
	_show_pair_link()
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
			# The game is over, but the SDK keeps running so it can deliver the final update and
			# the app can start the next game. Only the simulation pauses.
			set_process(false)
			return
		Scorbit.set_current_ball(_ball)
	Scorbit.commit()


func _on_game_start_requested(players: int) -> void:
	# A real game would start a game with this many players; this one always plays one.
	print("Scorbit app asked to start a %d-player game" % players)
	_start_game(Scorbit.STARTED_FROM_LOBBY)


func _on_credits_add_requested(credits: int, transaction: String) -> void:
	_credits += credits
	Scorbit.set_credits_dropped(credits, transaction)
	_report_credits()


func _report_credits() -> void:
	Scorbit.set_credits_status(false, _credits, 99)


# The pairing link carries the machine's UUID, which the SDK only knows once it has
# authenticated, so it is shown once the machine is known to be unpaired.
func _show_pair_link() -> void:
	if _pair_link_shown or Scorbit.get_status() != Scorbit.UNPAIRED:
		return
	_pair_link_shown = true
	print("Pair this machine: ", Scorbit.get_pair_deeplink())


func _on_players_updated(players: Dictionary) -> void:
	for number in players:
		var info: Dictionary = players[number]
		if info.get("has_info", false):
			print("Player %d: %s" % [number, info.get("preferred_name", "")])
		else:
			print("Player %d can claim at %s" % [number, info.get("claim_deeplink", "")])
