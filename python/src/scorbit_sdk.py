# python/src/scorbit_sdk.py

from .version import VERSION
from .net import Net
from .game_state import GameState, create_game_state, destroy_game_state
from .log import add_logger_callback, reset_logger, LogLevel
from .common_types import Ball, Player, Score
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
    def start_game():
        ScorbitSDK._current_game = GameState(ScorbitSDK._net_instance)

    @staticmethod
    async def update_game(active: bool, current_player: int=None, current_ball: int=None, 
                          player_scores: Dict[int, int]=None, game_modes: List[str]=None, callback=None):
        if ScorbitSDK._current_game:
            await ScorbitSDK._current_game.update(active, current_player, current_ball, player_scores, game_modes)
            
            if callback:
                callback()

            if not active:
                await ScorbitSDK._current_game.send_session_log()
                ScorbitSDK._current_game = None
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
            callback(result)
        
        return result
    
    _ws_callbacks = {}

    @staticmethod
    def set_ws_callback(command: str, callback: Callable):
        ScorbitSDK._ws_callbacks[command.upper()] = callback

    @staticmethod
    def set_ws_message_callback(callback: Callable):
        if ScorbitSDK._net_instance:
            ScorbitSDK._net_instance.set_ws_callback(callback)

    @staticmethod
    async def initialize(domain: str, provider: str, private_key: str, uuid: str, 
                         machine_serial: int, machine_id: int, software_version: str, 
                         developer_token: str = ""):
        ScorbitSDK._net_instance = Net()
        await ScorbitSDK._net_instance.initialize(domain, provider, private_key, uuid, 
                                                  machine_serial, machine_id, software_version, 
                                                  developer_token)
    
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
    def start():
        if ScorbitSDK._net_instance:
            asyncio.run(ScorbitSDK._net_instance.start())

    @staticmethod
    def create_game_state():
        if ScorbitSDK._net_instance:
            return create_game_state(ScorbitSDK._net_instance)
        else:
            raise Exception("Net instance not initialized. Call ScorbitSDK.initialize() first.")
        
    @staticmethod
    def version():
        return VERSION

    @staticmethod
    def create_game_state(net: Net):
        return create_game_state(net)

    @staticmethod
    def destroy_game_state():
        destroy_game_state()

    @staticmethod
    def add_logger_callback(callback, user_data=None):
        add_logger_callback(callback, user_data)

    @staticmethod
    def reset_logger():
        reset_logger()