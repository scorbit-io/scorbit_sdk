# python/src/game_state.py

import time
from typing import Dict
from .net_base import NetBase
from .game_data import GameData
from .player_state import PlayerState
from .modes import Modes, GameMode
import asyncio

class GameState:
    @property
    def modes(self) -> Modes:
        return self._data.modes
    
    def __init__(self, net: NetBase):
        self._net = net
        self._data = GameData()
        self._prev_data = GameData()
        self._start_time_ms = time.perf_counter_ns() // 1_000_000

    def update(self, active: bool, current_player: int = None, current_ball: int = None, 
               player_scores: Dict[int, int] = None, game_modes: Dict[str, GameMode] = None):
        if not active:
            self.set_game_finished()
            return

        if not self._data.is_game_started:
            self.set_game_started()

        self._data.session_sequence += 1
        self._update_session_time()

        if current_player is not None:
            self.set_active_player(current_player)
        if current_ball is not None:
            self.set_current_ball(current_ball)
        if player_scores:
            for player, score in player_scores.items():
                self.set_score(player, score)
        if game_modes is not None:
            self.clear_modes()
            for mode_string, mode in game_modes.items():
                self.add_mode(mode)

        self.commit()

    def set_game_started(self):
        if self._data.is_game_started:
            print("Game is already active, ignore starting game")
            return
        self._data = GameData()
        self._data.is_game_started = True
        self.set_current_ball(1)
        self.set_active_player(1)
        self._start_time_ms = time.perf_counter_ns() // 1_000_000
        self._data.session_sequence = 0
        self._update_session_time()

    def set_game_finished(self):
        self._data.is_game_started = False
        self._update_session_time()
        self._send_game_data()
        self._data = GameData()

    def set_current_ball(self, ball: int):
        if not self._is_ball_valid(ball):
            print(f"Ignoring attempt to set current ball to {ball}")
            return
        self._data.ball = ball
        self._update_session()

    def set_active_player(self, player: int):
        if not self._is_player_valid(player):
            print(f"Ignoring attempt to set active player to {player}")
            return
        self._data.active_player = player
        self._add_new_player(player)
        self._update_session()

    def set_score(self, player: int, score: int):
        if not self._is_player_valid(player):
            return
        self._add_new_player(player)
        self._data.players[player].score = score
        self._update_session()

    def add_mode(self, mode: GameMode):
        self._data.modes.add(mode)
        self._update_session()

    def remove_mode(self, mode: GameMode):
        self._data.modes.remove(mode)
        self._update_session()

    def clear_modes(self):
        self._data.modes.clear()
        self._update_session()

    def commit(self):
        self._send_game_data()

    def _update_session(self):
        self._data.session_sequence += 1
        self._update_session_time()

    def _update_session_time(self):
        self._data.session_time = time.perf_counter_ns() // 1_000_000 - self._start_time_ms

    def _add_new_player(self, player: int):
        if player not in self._data.players:
            self._data.players[player] = PlayerState(player, 0)
            print(f"Player {player} added")

    def _send_game_data(self):
        if self._is_changed():
            asyncio.run(self._net.send_game_data(self._data.__dict__))
            self._prev_data = self._data

    def _is_changed(self) -> bool:
        return self._data != self._prev_data

    @staticmethod
    def _is_player_valid(player: int) -> bool:
        return 1 <= player <= 9

    @staticmethod
    def _is_ball_valid(ball: int) -> bool:
        return 1 <= ball <= 9

# C-style interface functions
_game_state_instance = None

def create_game_state(net: NetBase):
    global _game_state_instance
    _game_state_instance = GameState(net)
    return _game_state_instance

def destroy_game_state():
    global _game_state_instance
    _game_state_instance = None

# Wrapper functions for C-style interface
def set_game_started():
    if _game_state_instance:
        _game_state_instance.set_game_started()

def set_game_finished():
    if _game_state_instance:
        _game_state_instance.set_game_finished()

def set_current_ball(ball: int):
    if _game_state_instance:
        _game_state_instance.set_current_ball(ball)

def set_active_player(player: int):
    if _game_state_instance:
        _game_state_instance.set_active_player(player)

def set_score(player: int, score: int):
    if _game_state_instance:
        _game_state_instance.set_score(player, score)

def add_mode(mode: GameMode):
    if _game_state_instance:
        _game_state_instance.add_mode(mode)

def remove_mode(mode: GameMode):
    if _game_state_instance:
        _game_state_instance.remove_mode(mode)

def clear_modes():
    if _game_state_instance:
        _game_state_instance.clear_modes()

def commit():
    if _game_state_instance:
        _game_state_instance.commit()