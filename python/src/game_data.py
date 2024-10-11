from typing import Dict
from .player_state import PlayerState
from .modes import Modes

class GameData:
    def __init__(self):
        self.is_game_started: bool = False
        self.active_player: int = 1
        self.ball: int = 1
        self.players: Dict[int, PlayerState] = {}
        self.modes: Modes = Modes()
        self.session_sequence: int = 0  # New field
        self.session_time: int = 0  # New field

    def __eq__(self, other):
        if not isinstance(other, GameData):
            return False
        return (self.is_game_started == other.is_game_started and
                self.active_player == other.active_player and
                self.ball == other.ball and
                self.players == other.players and
                self.modes == other.modes and
                self.session_sequence == other.session_sequence and  # Compare new field
                self.session_time == other.session_time)  # Compare new field

    def __ne__(self, other):
        return not self.__eq__(other)