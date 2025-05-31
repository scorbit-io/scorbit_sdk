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

def setup_game_state():
    info = scorbit.DeviceInfo()
    info.provider = "dilshodpinball"
    info.machine_id = 4379
    info.hostname = "staging"   # staging, production, or specific url
    info.game_code_version = "0.1.0"
    info.uuid = "c7f1fd0b-82f7-5504-8fbe-740c09bc7dab" # don't set it, then it will be generated automatically

    # Automatically download pictures, will be available in PlayerInfo.picture
    info.auto_download_player_pics = True

    info.score_features = G_SCORE_FEATURES
    info.score_features_version = G_SCORE_FEATURES_VERSION

    # Use encrypt_tool to generate your encrypted key from your private key
    encrypted_key = '8qWNpMPeO1AbgcoPSsdeUORGmO/hyB70oyrpFyRlYWbaVx4Kuan0CAGaXZWS3JWdgmPL7p9k3UFTwAp5y16L8O1tYaHLGkW4p/yWmA=='
    return scorbit.create_game_state(encrypted_key, info)

def check_players_info(gs, players, players_num):
    if gs.is_players_info_updated():
        for j in range(1, players_num + 1):
            if gs.has_player_info(j):
                players[j] = gs.get_player_info(j)
            else:
                players.pop(j, None)  # Erase player info if it was removed or not found
    return players

def main():
    players = {} # Dictionary to hold player information

    print(f"Simple example of Scorbit SDK {scorbit.__version__} usage")

    # Setup logger
    scorbit.add_logger_callback(logger_callback)

    # Create game state object
    gs = setup_game_state()

    gs.request_pair_code(lambda error, short_code: print(
        f"Pairing short code: {short_code}" if error == 0 else f"Error: {error}"
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

        if i == 50:
            print(f"Deeplink for claiming: {gs.get_claim_deeplink(1)}")

        if is_unpair_triggered_by_user():
            gs.request_unpair(lambda error, reply: print(
                "Unpairing successful" if error == 0 else f"Error: {error}"
            ))

        if is_game_just_started(i):
            gs.set_game_started()

        if is_game_active(i):
            # Let's pretend that this players_num is current number of players in the game
            players_num = 1

            check_players_info(gs, players, players_num)

            # Display players names / pictures
            for player_num, player_info in players.items():
                print(f"Player {player_num}: id: {player_info.id}, name: {player_info.name}, initials: {player_info.initials}, preferred_name: {player_info.preferred_name}")
                print(f"Picture size: {len(player_info.picture)} bytes, url: {player_info.picture_url}")
                # Print first 32 bytes of picture in hex format
                if (len(player_info.picture) > 0):
                    print(f"Player {player_num} picture (first 32 bytes): {player_info.picture[:32].hex()}")

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
