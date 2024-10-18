from .version import VERSION
from .net import Net
from .game_state import GameState
from .log import add_logger_callback, reset_logger, LogLevel
from .common_types import Ball, Player, Score
from typing import Dict, List
from .session_logger import SessionLogger
import os

class ScorbitSDK:
    _net_instance = None
    _current_game = None

    DEBUG: bool = True

    DOMAIN_PRODUCTION = "api.scorbit.io"
    DOMAIN_STAGING = "staging.scorbit.io"

    METHOD_GET = "GET"
    METHOD_POST = "POST"
    METHOD_DELETE = "DELETE"
    METHOD_PUT = "PUT"
    METHOD_PATCH = "PATCH"

    @staticmethod
    async def initialize(domain: str, provider: str, private_key: str, uuid: str,
                         machine_serial: int, machine_id: int, software_version: str):
        if not ScorbitSDK._net_instance:
            ScorbitSDK._net_instance = Net()
        await ScorbitSDK._net_instance.initialize(domain, provider, private_key, uuid,
                                                  machine_serial, machine_id, software_version)
        print("Scorbit initialized successfully")

    @staticmethod
    async def send_installed_data(data: dict):
        if ScorbitSDK._net_instance:
            return await ScorbitSDK._net_instance.send_installed_data(data)
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    async def get_config():
        if ScorbitSDK._net_instance:
            return await ScorbitSDK._net_instance.get_config()
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    async def get_achievements():
        if ScorbitSDK._net_instance:
            return await ScorbitSDK._net_instance.get_achievements()
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    async def unlock_achievement(user_id: str, achievement_id: str):
        if ScorbitSDK._net_instance:
            return await ScorbitSDK._net_instance.unlock_achievement(user_id, achievement_id)
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    async def increment_achievement(achievement_id: str, increment: int):
        if ScorbitSDK._net_instance:
            return await ScorbitSDK._net_instance.increment_achievement(achievement_id, increment)
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    async def set_achievement_achieved(achievement_id: str):
        if ScorbitSDK._net_instance:
            return await ScorbitSDK._net_instance.set_achievement_achieved(achievement_id)
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    async def start_heartbeat(interval=10):
        if ScorbitSDK._net_instance:
            await ScorbitSDK._net_instance.start_heartbeat(interval)
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    async def stop_heartbeat():
        if ScorbitSDK._net_instance:
            await ScorbitSDK._net_instance.stop_heartbeat()
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    async def start():
        if not ScorbitSDK._net_instance:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")
        await ScorbitSDK._net_instance.start()
        
    @staticmethod
    def create_game_state():
        if ScorbitSDK._net_instance:
            ScorbitSDK._current_game = GameState(ScorbitSDK._net_instance)
            return ScorbitSDK._current_game
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    def destroy_game_state():
        ScorbitSDK._current_game = None

    @staticmethod
    async def set_game_started():
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.set_game_started()
            if ScorbitSDK._session_logger is None:
                ScorbitSDK._session_logger = SessionLogger(uuid=os.getenv("SCORBIT_UUID"))
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def log_event(current_scores, current_player, current_ball, game_modes):
        if ScorbitSDK._session_logger:
            ScorbitSDK._session_logger.log_event(current_scores, current_player, current_ball, game_modes)

    @staticmethod
    async def set_game_finished():
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.set_game_finished()
            if ScorbitSDK._session_logger:
                ScorbitSDK._session_logger.save_log()
                await ScorbitSDK.upload_session_log(ScorbitSDK._session_logger.log_file_path)
                ScorbitSDK._session_logger = None
        else:
            raise Exception("No active game. Call set_game_started() first.")


    @staticmethod
    async def set_current_ball(ball: int):
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.set_current_ball(ball)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def set_active_player(player: int):
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.set_active_player(player)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def set_score(player: int, score: int):
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.set_score(player, score)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def post_score(score_data: dict):
        if ScorbitSDK._net_instance:
            return await ScorbitSDK._net_instance.post_score(score_data)
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    async def add_mode(mode: str):
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.add_mode(mode)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def remove_mode(mode: str):
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.remove_mode(mode)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def clear_modes():
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.clear_modes()
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def commit():
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.commit()
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def update_game(commit: bool = True, current_player: int = None, current_ball: int = None,
                        player_scores: Dict[int, int] = None, game_modes: List[str] = None):
        if ScorbitSDK._current_game:
            if current_player is not None:
                await ScorbitSDK.set_active_player(current_player)
            if current_ball is not None:
                await ScorbitSDK.set_current_ball(current_ball)
            if player_scores:
                for player, score in player_scores.items():
                    await ScorbitSDK.set_score(player, score)
            if game_modes is not None:
                await ScorbitSDK.clear_modes()
                for mode in game_modes:
                    await ScorbitSDK.add_mode(mode)
            if commit:
                await ScorbitSDK.commit()
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def tick():
        if ScorbitSDK._net_instance:
            await ScorbitSDK._net_instance.process_messages()
    
    @staticmethod
    def get_pairing_qr_url(manufacturer_prefix, scorbit_machine_id, scorbitron_uuid):
        return f"https://scorbit.link/qrcode?$deeplink_path={manufacturer_prefix}&machineid={scorbit_machine_id}&uuid={scorbitron_uuid}"

    @staticmethod
    def get_claiming_qr_url(venuemachine_id, opdb_id):
        return f"https://scorbit.link/qrcode?$deeplink_path={venuemachine_id}&opdb={opdb_id}"

    @staticmethod
    async def start():
        if ScorbitSDK._net_instance:
            await ScorbitSDK._net_instance.start()

    @staticmethod
    def version():
        return VERSION

    @staticmethod
    def add_logger_callback(callback, user_data=None):
        add_logger_callback(callback, user_data)

    @staticmethod
    def reset_logger():
        reset_logger()

    @staticmethod
    async def close():
        if ScorbitSDK._net_instance:
            await ScorbitSDK._net_instance.close()
            ScorbitSDK._net_instance = None

    @staticmethod
    async def unlock_achievement(user_id: str, achievement_id: str):
        if ScorbitSDK._net_instance:
            data = {
                "user_id": user_id,
                "achievement_id": achievement_id
            }
            return await ScorbitSDK._net_instance.api_call("POST", f"/api/achievements/unlock/", data=data, authorization=True)
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    async def increment_achievement(achievement_id: str, increment: int):
        if ScorbitSDK._net_instance:
            data = {
                "achievement_id": achievement_id,
                "increment": increment
            }
            return await ScorbitSDK._net_instance.api_call("POST", "/api/achievements/increment/", data=data, authorization=True)
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

    @staticmethod
    async def set_achievement_achieved(achievement_id: str):
        if ScorbitSDK._net_instance:
            return await ScorbitSDK._net_instance.api_call("POST", f"/api/achievements/achieve/{achievement_id}/", authorization=True)
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")

# Expose ScorbitSDK methods for easier access
DOMAIN_PRODUCTION = ScorbitSDK.DOMAIN_PRODUCTION
DOMAIN_STAGING = ScorbitSDK.DOMAIN_STAGING
METHOD_GET = ScorbitSDK.METHOD_GET
METHOD_POST = ScorbitSDK.METHOD_POST
METHOD_DELETE = ScorbitSDK.METHOD_DELETE
METHOD_PUT = ScorbitSDK.METHOD_PUT
METHOD_PATCH = ScorbitSDK.METHOD_PATCH
initialize = ScorbitSDK.initialize
start = ScorbitSDK.start
tick = ScorbitSDK.tick
create_game_state = ScorbitSDK.create_game_state
destroy_game_state = ScorbitSDK.destroy_game_state
set_game_started = ScorbitSDK.set_game_started
set_game_finished = ScorbitSDK.set_game_finished
set_current_ball = ScorbitSDK.set_current_ball
set_active_player = ScorbitSDK.set_active_player
set_score = ScorbitSDK.set_score
add_mode = ScorbitSDK.add_mode
remove_mode = ScorbitSDK.remove_mode
clear_modes = ScorbitSDK.clear_modes
commit = ScorbitSDK.commit
update_game = ScorbitSDK.update_game
get_pairing_qr_url = ScorbitSDK.get_pairing_qr_url
get_claiming_qr_url = ScorbitSDK.get_claiming_qr_url
version = ScorbitSDK.version
add_logger_callback = ScorbitSDK.add_logger_callback
reset_logger = ScorbitSDK.reset_logger