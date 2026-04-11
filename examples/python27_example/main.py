# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

from __future__ import absolute_import, print_function

import time
from datetime import datetime
import scorbit

# Set score features - optional, but if you have features that help identify what
# triggered a score increase in GameState.set_score().
# WARNING: in the future releases we can ONLY add new features, but not remove
# existing ones, otherwise indices of score features of old game sessions will be
# broken. Also we should increment G_SCORE_FEATURES_VERSION when new feature(s) added.
G_SCORE_FEATURES = ["ramp", "left spinner", "right spinner", "left slingshot", "right slingshot"]
G_SCORE_FEATURES_VERSION = 1

G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM = 0

# Mutable holder so the event callback can reference the game state even though
# the callback is registered on Config before create_game_state() is called.
gs_holder = [None]

# -------- Dummy functions to simulate game state --------
def is_game_finished(i):
    return i == 99

def is_game_just_started(i):
    return i == 5

def is_game_active(i):
    return 5 <= i < 99

def player1_score(i):
    return 0 if i == 5 else 1000 + i * 500

def has_player2():
    return False

def player2_score():
    return 2000

def has_player3():
    return False

def player3_score():
    return 3000

def has_player4():
    return False

def player4_score():
    return 4000

def current_player():
    return 1

def current_ball(i):
    return i // 33 + 1

def time_to_clear_modes():
    return False

def is_unpair_triggered_by_user():
    return False

# -------- Logger callback --------
def logger_callback(message, level, file, line, timestamp):
    if level == scorbit.LogLevel.Debug:
        return
    level_str = {scorbit.LogLevel.Debug: "DBG", scorbit.LogLevel.Info: "INF",
                 scorbit.LogLevel.Warn: "WRN", scorbit.LogLevel.Error: "ERR"}.get(level, "UNK")
    dt = datetime.fromtimestamp(timestamp / 1000)
    print("[%s] [%s] %s" % (dt.strftime('%Y-%m-%d %H:%M:%S'), level_str, message))

# -------- Key persistence callbacks --------
# These callbacks are used to save and load a key to/from persistent storage.
# The SDK will call these when it needs to persist or retrieve the key.

KEY_FILE_PATH = "scorbit_key.txt"

def save_key_callback(key):
    print("Saving key to file: %s" % KEY_FILE_PATH)
    try:
        with open(KEY_FILE_PATH, 'w') as f:
            f.write(key)
    except Exception as e:
        print("Failed to save key: %s" % e)

def load_key_callback():
    print("Loading key from file: %s" % KEY_FILE_PATH)
    try:
        with open(KEY_FILE_PATH, 'r') as f:
            return f.read()
    except IOError:
        return ""
    except Exception as e:
        print("Failed to load key: %s" % e)
        return ""

# -------- Event callback --------
def events_callback(event):
    global G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM

    gs = gs_holder[0]
    print("Event received: %s" % event.type)

    if event.type == scorbit.EventType.GameStartRequested:
        players_count = event.get_game_start_requested()
        if players_count is not None:
            print("Game start requested with %d player(s)" % players_count)
            G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM = players_count

    elif event.type == scorbit.EventType.CreditsAddRequested:
        result = event.get_credits_add_requested()
        if result is not None:
            credits_to_add, transaction = result
            print("Credits add requested: %d credit(s)" % credits_to_add)
            if gs:
                gs.set_credits_dropped(credits_to_add, transaction, True)

    elif event.type == scorbit.EventType.CreditsStatusRequested:
        print("Credits status requested")
        if gs:
            gs.set_credits_status(False, 10, 20, "")

    elif event.type == scorbit.EventType.PlayersUpdated:
        players_dict = event.get_players_updated()
        if players_dict is not None:
            print("Players updated, count: %d" % len(players_dict))
            for num, info in players_dict.items():
                if info.has_info():
                    print("  Player %d: %s (id: %s)" % (num, info.preferred_name, info.id))
                else:
                    print("  Player %d: unclaimed, claim at %s" % (num, info.claim_deeplink))

    elif event.type == scorbit.EventType.PlayerPictureReady:
        result = event.get_player_picture_ready()
        if result is not None:
            player_num, picture = result
            print("Player %d picture ready: %d bytes" % (player_num, len(picture)))

    elif event.type == scorbit.EventType.ConfigReceived:
        config_json = event.get_config_received()
        if config_json is not None:
            print("Config received: %s" % config_json)

        payments_enabled = event.get_config_payments_enabled()
        if payments_enabled is not None:
            print("Payments enabled: %s" % payments_enabled)

def _pair_code_cb(error, short_code):
    if error == scorbit.Error.Success:
        print("Pairing short code: %s" % short_code)
    else:
        print("Error: %s" % error)

def _top_scores_cb(error, reply):
    if error == scorbit.Error.Success:
        print("Top scores: %s" % reply)
    else:
        print("Error: %s" % error)

def _unpair_cb(error, reply):
    if error == scorbit.Error.Success:
        print("Unpairing successful")
    else:
        print("Error: %s" % error)

def setup_game_state():
    config = scorbit.Config()
    config.set_provider("dilshodpinball")
    config.set_machine_id(4379)
    config.set_hostname("staging")
    config.set_game_code_version("0.1.0")
    config.set_auto_download_player_pics(True)
    config.set_score_features(G_SCORE_FEATURES, G_SCORE_FEATURES_VERSION)

    config.set_encrypted_key('8qWNpMPeO1AbgcoPSsdeUORGmO/hyB70oyrpFyRlYWbaVx4Kuan0CAGaXZWS3JWdgmPL7p9k3UFTwAp5y16L8O1tYaHLGkW4p/yWmA==')

    config.set_save_key_callback(save_key_callback)
    config.set_load_key_callback(load_key_callback)

    # Event callback must be set on Config before create_game_state().
    # We use gs_holder so the callback can reference the game state once it exists.
    config.set_event_callback(events_callback)

    gs = scorbit.create_game_state(config)
    gs_holder[0] = gs
    return gs

def main():
    global G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM

    print("Simple example of Scorbit SDK %s usage" % scorbit.__version__)

    # Setup logger
    scorbit.add_logger_callback(logger_callback, 512)

    gs = setup_game_state()

    # Set capabilities
    gs.set_capabilities(scorbit.Capability.StartGame | scorbit.Capability.CreditDrop)

    gs.request_pair_code(_pair_code_cb)
    gs.request_top_scores(0, _top_scores_cb)

    print("Deeplink for pairing: %s" % gs.pair_deeplink)

    for i in range(100):
        if i % 10 == 0:
            print("Networking status: %s" % gs.status)

        if is_game_finished(i):
            gs.set_game_finished()

        if is_unpair_triggered_by_user():
            gs.request_unpair(_unpair_cb)

        if is_game_just_started(i):
            gs.set_game_started(scorbit.GameStartOrigin.StartButton)
        else:
            if G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM > 0:
                players_count = G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM
                G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM = 0
                for j in range(players_count):
                    gs.set_score(j + 1, 0)
                gs.set_game_started(scorbit.GameStartOrigin.FromLobby)
                print("Game start requested with %d players!" % players_count)

        if is_game_active(i):
            gs.set_score(1, player1_score(i), 2)
            if has_player2(): gs.set_score(2, player2_score(), 1)
            if has_player3(): gs.set_score(3, player3_score(), 1)
            if has_player4(): gs.set_score(4, player4_score(), 1)

            gs.set_active_player(current_player())
            gs.set_current_ball(current_ball(i))

            if i % 10 == 0:
                gs.add_mode("MB:Multiball")
            else:
                gs.remove_mode("MB:Multiball")

            gs.add_mode_expiring("MB:Multiball", 3)

            if time_to_clear_modes():
                gs.clear_modes()

        print("Commit cycle %d" % i)
        gs.commit()

        time.sleep(0.5)

    gs.destroy()
    print("Example finished")

if __name__ == "__main__":
    main()
