from .scorbit_sdk import ScorbitSDK, initialize, start, tick
from .net import Net
from .game_state import GameState
from .modes import Modes, GameMode
from .log import LogLevel, DEBUG, INFO, WARN, ERROR
from .version import VERSION
from .common_types import Ball, Player, Score
from .version import VERSION
from .session_logger import SessionLogger

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
    'set_game_started',
    'set_game_finished',
    'set_current_ball',
    'set_active_player',
    'set_score',
    'add_mode',
    'remove_mode',
    'clear_modes',
    'commit',
    'start_heartbeat',
    'stop_heartbeat',
    'unlock_achievement',
    'increment_achievement',
    'set_achievement_achieved',
    'close',
    'save_session_log',
    'log_event',
    'get_achievements',
    'send_installed_data',
    'get_config',
    'send_session_log',
    'Net',
    'GameState',
    'Modes',
    'GameMode',
    'LogLevel',
    'SessionLogger',
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
send_installed_data = ScorbitSDK.send_installed_data
get_config = ScorbitSDK.get_config
send_session_log = ScorbitSDK.send_session_log
unlock_achievement = ScorbitSDK.unlock_achievement
increment_achievement = ScorbitSDK.increment_achievement
set_achievement_achieved = ScorbitSDK.set_achievement_achieved
close = ScorbitSDK.close
save_session_log = ScorbitSDK.save_session_log
log_event = ScorbitSDK.log_event
get_achievements = ScorbitSDK.get_achievements

# Expose constants
DOMAIN_PRODUCTION = ScorbitSDK.DOMAIN_PRODUCTION
DOMAIN_STAGING = ScorbitSDK.DOMAIN_STAGING
METHOD_GET = ScorbitSDK.METHOD_GET
METHOD_POST = ScorbitSDK.METHOD_POST
METHOD_DELETE = ScorbitSDK.METHOD_DELETE
METHOD_PUT = ScorbitSDK.METHOD_PUT
METHOD_PATCH = ScorbitSDK.METHOD_PATCH