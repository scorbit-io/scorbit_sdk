# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scrobit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

import time
from datetime import datetime
from scorbit import scorbit

# Set score features - optional, but if you have features that help identify what
# triggered a score increase in GameState.setScore().
# WARNING: in the future releasees we can ONLY add new features, but not remove
# existing ones, otherwise indices of score features of old game sesions will be
# broken. Also we should increment scoreFeaturesVersion when new feature(s) added.
G_SCORE_FEATURES = ["ramp", "left spinner", "right spinner", "left slingshot", "right slingshot"]
G_SCORE_FEATURES_VERSION = 1

G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM = 0

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
    level_str = {scorbit.LogLevel.Debug: "DBG", scorbit.LogLevel.Info: "INF",
                 scorbit.LogLevel.Warn: "WRN", scorbit.LogLevel.Error: "ERR"}.get(level, "UNK")
    dt = datetime.fromtimestamp(timestamp / 1000) # timestamp is in milliseconds
    print(f"[{dt.strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]}] [{level_str}] {message}")

# -------- Key persistence callbacks --------
# These callbacks are used to save and load a key to/from persistent storage.
# The SDK will call these when it needs to persist or retrieve the key.

KEY_FILE_PATH = "scorbit_key.txt"

def save_key_callback(key):
    print(f"Saving key to file: {KEY_FILE_PATH}")
    try:
        with open(KEY_FILE_PATH, 'w') as f:
            f.write(key)
    except Exception as e:
        print(f"Failed to save key: {e}")

def load_key_callback():
    print(f"Loading key from file: {KEY_FILE_PATH}")
    try:
        with open(KEY_FILE_PATH, 'r') as f:
            return f.read()
    except FileNotFoundError:
        # Return empty string if file doesn't exist (no key saved yet)
        return ""
    except Exception as e:
        print(f"Failed to load key: {e}")
        return ""

# -------- Event callback --------
def events_callback(gs, event):
    global G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM

    print(f"Event received: {event.type()}")
    
    if event.type() == scorbit.EventType.GameStartRequested:
        success, players_count = event.get_game_start_requested()
        if success:
            print(f"Game start requested with {players_count} player(s)")
            G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM = players_count
    
    elif event.type() == scorbit.EventType.CreditsAddRequested:
        success, credits_to_add, transaction = event.get_credits_add_requested()
        if success:
            print(f"Credits add requested: {credits_to_add} credit(s)")
            # Add credits to the machine here...
            # And then confirm adding credits:
            gs.set_credits_dropped(credits_to_add, transaction, True)
    
    elif event.type() == scorbit.EventType.CreditsStatusRequested:
        # This event is sent when backend requests credits status.
        # Using GameState.set_credits_status()
        print("Credits status requested")
        gs.set_credits_status(False, 10, 20, "")
    
    elif event.type() == scorbit.EventType.PlayersUpdated:
        success, players_dict = event.get_players_updated()
        if success:
            print(f"Players updated, count: {len(players_dict)}")
            for num, info in players_dict.items():
                if info.has_info():
                    print(f"  Player {num}: {info.preferred_name} (id: {info.id})")
                else:
                    print(f"  Player {num}: unclaimed, claim at {info.claim_deeplink}")

    elif event.type() == scorbit.EventType.PlayerPictureReady:
        success, player_num, picture = event.get_player_picture_ready()
        if success:
            print(f"Player {player_num} picture ready: {len(picture)} bytes")

    elif event.type() == scorbit.EventType.ConfigReceived:
        success, config_json = event.event_config_received()
        if success:
            print(f"Config received: {config_json}")
            # Process config JSON...
        else:
            print("Error getting config event")

        success, payments_enabled = event.get_config_payments_enabled()
        if success:
            print(f"Payments enabled: {payments_enabled}")

def setup_game_state():
    # Create config with all settings including authentication
    config = scorbit.Config()
    config.set_provider("dilshodpinball")
    config.set_machine_id(4379)
    config.set_hostname("staging")  # staging, production, or specific url
    config.set_game_code_version("0.1.0")
    config.set_uuid("c7f1fd0b-82f7-5504-8fbe-740c09bc7dab")  # optional, auto-generated if not set
    config.set_auto_download_player_pics(True)
    config.set_score_features(G_SCORE_FEATURES, G_SCORE_FEATURES_VERSION)

    # Set authentication - encrypted key generated by encrypt_tool
    config.set_encrypted_key('8qWNpMPeO1AbgcoPSsdeUORGmO/hyB70oyrpFyRlYWbaVx4Kuan0CAGaXZWS3JWdgmPL7p9k3UFTwAp5y16L8O1tYaHLGkW4p/yWmA==')

    # Setup key persistence callbacks - SDK will use these to save/load keys
    config.set_save_key_callback(save_key_callback)
    config.set_load_key_callback(load_key_callback)

    return scorbit.create_game_state(config)

def main():
    global G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM

    print(f"Simple example of Scorbit SDK {scorbit.__version__} usage")

    # Setup logger
    scorbit.add_logger_callback(logger_callback, 512) # 512 bytes max message length)

    # Create game state object
    gs = setup_game_state()

    # Set capabilities. Here we set both start game and credit drop capabilities
    gs.set_capabilities(scorbit.Capability.StartGame | scorbit.Capability.CreditDrop)

    # Setup events callback
    gs.set_event_callback(lambda event: events_callback(gs, event))

    gs.request_pair_code(lambda error, short_code: print(
        f"Pairing short code: {short_code}" if error == scorbit.Error.Success else f"Error: {error}"
    ))

    gs.request_top_scores(0, lambda error, reply: print(
        f"Top scores: {reply}" if error == scorbit.Error.Success else f"Error: {error}"
    ))

    print(f"Deeplink for pairing: {gs.get_pair_deeplink()}")

    for i in range(100):
        if i % 10 == 0:
            print(f"Networking status: {gs.get_status()}")

        if is_game_finished(i):
            gs.set_game_finished()

        if is_unpair_triggered_by_user():
            gs.request_unpair(lambda error, reply: print(
                "Unpairing successful" if error == scorbit.Error.Success else f"Error: {error}"
            ))

        if is_game_just_started(i): # started by Start Button
            gs.set_game_started(scorbit.GameStartOrigin.StartButton)
        else:
            if G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM > 0:
                players_count = G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM
                G_GAME_START_REQUESTED_FROM_LOBBY_PLAYERS_NUM = 0
                # Game was started from the app and requested to start the game on the machine
                # call function to start the game on the machine with players_count players ...

                # It's not necessary to call gs.set_game_started(), as it's automaticlly called when
                # request arrived and will be be ignored here
                for i in range(players_count):
                    gs.set_score(i + 1, 0) # Initialize player scores to 0
                gs.set_game_started(scorbit.GameStartOrigin.FromLobby)
                print(f"Game start requested with {players_count} players!")


        if is_game_active(i):
            gs.set_score(1, player1_score(i), 2) # 2 is the score feature index for "right spinner"
            if has_player2(): gs.set_score(2, player2_score(), 1)
            if has_player3(): gs.set_score(3, player3_score(), 1)
            if has_player4(): gs.set_score(4, player4_score(), 1)

            gs.set_active_player(current_player())
            gs.set_current_ball(current_ball(i))

            if i % 10 == 0:
                gs.add_mode("MB:Multiball")
            else:
                gs.remove_mode("MB:Multiball")

            if time_to_clear_modes():
                gs.clear_modes()

        print(f"Commit cycle {i}")
        gs.commit()

        time.sleep(0.5)

    print("Example finished")

if __name__ == "__main__":
    main()
