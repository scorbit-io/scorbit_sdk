from .version import VERSION
from .net import Net
from .game_state import GameState, create_game_state, destroy_game_state
from .log import add_logger_callback, reset_logger, LogLevel
from .common_types import Ball, Player, Score
from .messages import ScorbitRESTResponse, ScorbitWSMessage
import asyncio
from typing import Callable, Dict, List

class ScorbitSDK:
    _net_instance = None
    _api_call_callbacks = {}
    _api_call_number = 0
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
                        machine_serial: int, machine_id: int, software_version: str, 
                        developer_token: str = ""):
        ScorbitSDK._net_instance = Net()
        await ScorbitSDK._net_instance.initialize(domain, provider, private_key, uuid, 
                                                machine_serial, machine_id, software_version, 
                                                developer_token)

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
    def set_game_started():
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.set_game_started()
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def set_game_finished():
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.set_game_finished()
            ScorbitSDK._current_game.send_session_log()
            ScorbitSDK._current_game = None
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def set_current_ball(ball: int):
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.set_current_ball(ball)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def set_active_player(player: int):
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.set_active_player(player)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def set_score(player: int, score: int):
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.set_score(player, score)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def add_mode(mode: str):
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.add_mode(mode)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def remove_mode(mode: str):
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.remove_mode(mode)
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    def clear_modes():
        if ScorbitSDK._current_game:
            ScorbitSDK._current_game.clear_modes()
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def commit():
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.commit()
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def update_game(active: bool, current_player: int=None, current_ball: int=None, 
                          player_scores: Dict[int, int]=None, game_modes: List[str]=None, callback=None):
        if ScorbitSDK._current_game:
            if current_player is not None:
                ScorbitSDK.set_active_player(current_player)
            if current_ball is not None:
                ScorbitSDK.set_current_ball(current_ball)
            if player_scores:
                for player, score in player_scores.items():
                    ScorbitSDK.set_score(player, score)
            if game_modes:
                ScorbitSDK.clear_modes()
                for mode in game_modes:
                    ScorbitSDK.add_mode(mode)
            await ScorbitSDK.commit()
            if callback:
                callback()
            if not active:
                ScorbitSDK.set_game_finished()
        else:
            raise Exception("Game state not created. Call create_game_state() first.")

    @staticmethod
    async def tick():
        if ScorbitSDK._net_instance:
            await ScorbitSDK._net_instance.process_messages()

    @staticmethod
    async def api_call(method: str, endpoint: str, data=None, files=None, authorization: bool=False, callback=None):
        if not ScorbitSDK._net_instance:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")
        
        ScorbitSDK._api_call_number += 1
        ScorbitSDK._api_call_callbacks[ScorbitSDK._api_call_number] = callback
        
        result = await ScorbitSDK._net_instance.api_call(method, endpoint, data, files, authorization)
        
        if callback:
            await callback(result)
        
        return result
    
    _ws_callbacks = {}

    @staticmethod
    def set_ws_callback(command: str, callback: Callable):
        ScorbitSDK._ws_callbacks[command.upper()] = callback

    @staticmethod
    async def set_ws_message_callback(callback):
        if ScorbitSDK._net_instance:
            await ScorbitSDK._net_instance.set_ws_callback(callback)
    
    @staticmethod
    async def ws_command(command: str, data: dict, specific_callback=None):
        if not ScorbitSDK._net_instance:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")
        
        if specific_callback:
            ScorbitSDK._net_instance.set_ws_callback(command, specific_callback)
        
        await ScorbitSDK._net_instance.ws_send(command.upper(), data)
    
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
api_call = ScorbitSDK.api_call
set_ws_message_callback = ScorbitSDK.set_ws_message_callback
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