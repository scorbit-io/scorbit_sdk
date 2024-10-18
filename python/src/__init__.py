from .scorbit_sdk import ScorbitSDK, initialize, start, tick, api_call, set_ws_message_callback
from .net import Net
from .game_state import GameState
from .modes import Modes, GameMode
from .log import LogLevel, DEBUG, INFO, WARN, ERROR
from .version import VERSION
from .messages import ScorbitRESTMessage, ScorbitRESTResponse, ScorbitWSMessage, ScorbitWebSocketResponse
from .common_types import Ball, Player, Score
from .version import VERSION

__all__ = [
    'ScorbitSDK',
    'initialize',
    'start',
    'tick',
    'start_game',
    'update_game',
    'get_pairing_qr_url',
    'get_claiming_qr_url',
    'version',
    'create_game_state',
    'destroy_game_state',
    'add_logger_callback',
    'reset_logger',
    'Net',
    'GameState',
    'Modes',
    'GameMode',
    'LogLevel',
    'DEBUG',
    'INFO',
    'WARN',
    'ERROR',
    'VERSION',
    'Ball',
    'Player',
    'Score',
    'DOMAIN_PRODUCTION',
    'DOMAIN_STAGING',
    'METHOD_GET',
    'METHOD_POST',
    'METHOD_DELETE',
    'METHOD_PUT',
    'METHOD_PATCH'
]

# Expose ScorbitSDK methods for easier access
initialize = ScorbitSDK.initialize
start = ScorbitSDK.start
tick = ScorbitSDK.tick
update_game = ScorbitSDK.update_game
get_pairing_qr_url = ScorbitSDK.get_pairing_qr_url
get_claiming_qr_url = ScorbitSDK.get_claiming_qr_url
version = ScorbitSDK.version
create_game_state = ScorbitSDK.create_game_state
destroy_game_state = ScorbitSDK.destroy_game_state
add_logger_callback = ScorbitSDK.add_logger_callback
reset_logger = ScorbitSDK.reset_logger
set_game_started = ScorbitSDK.set_game_started
set_game_finished = ScorbitSDK.set_game_finished
set_current_ball = ScorbitSDK.set_current_ball
set_active_player = ScorbitSDK.set_active_player
set_score = ScorbitSDK.set_score
add_mode = ScorbitSDK.add_mode
remove_mode = ScorbitSDK.remove_mode
clear_modes = ScorbitSDK.clear_modes
commit = ScorbitSDK.commit

# Expose constants
DOMAIN_PRODUCTION = ScorbitSDK.DOMAIN_PRODUCTION
DOMAIN_STAGING = ScorbitSDK.DOMAIN_STAGING
METHOD_GET = ScorbitSDK.METHOD_GET
METHOD_POST = ScorbitSDK.METHOD_POST
METHOD_DELETE = ScorbitSDK.METHOD_DELETE
METHOD_PUT = ScorbitSDK.METHOD_PUT
METHOD_PATCH = ScorbitSDK.METHOD_PATCH