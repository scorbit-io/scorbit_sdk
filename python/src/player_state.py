# python/src/player_state.py

class PlayerState:
    def __init__(self, player: int, score: int = 0):
        self.player: int = player
        self.score: int = score

    def __eq__(self, other):
        if not isinstance(other, PlayerState):
            return NotImplemented
        return self.player == other.player and self.score == other.score

    def __ne__(self, other):
        return not (self == other)