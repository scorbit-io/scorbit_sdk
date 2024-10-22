# python/src/modes.py

from typing import Set

class GameMode:
    CATEGORY_MULTIBALL = "MB"
    CATEGORY_BALL_LOCKED = "BL"
    CATEGORY_WIZARD_MODE = "WM"
    CATEGORY_EXTRA_BALL = "EB"
    CATEGORY_BONUS_MULTIPLIER = "BX"
    CATEGORY_OTHER = "NA"
    CATEGORY_COMPLETED = "CP"
    CATEGORY_HIDE_FROM_SESSION_LOG = "XX"
    CATEGORY_HIDE_FROM_CLIENTS = "ZZ"
    CATEGORY_OVERRIDE_GAME_ON = "XY"

    COLOR_RED = "red"
    COLOR_ORANGE = "orange"
    COLOR_YELLOW = "yellow"
    COLOR_GREEN = "green"
    COLOR_BLUE = "blue"
    COLOR_PURPLE = "purple"
    COLOR_PINK = "pink"

    def __init__(self, category: str, text: str, color: str = None, number: int = None):
        self.category = category
        self.text = text
        self.color = color
        self.number = number

    def get_string(self) -> str:
        string = self.category
        if self.number is not None:
            string += str(self.number)
        if self.color is not None:
            string += f"{{{self.color}}}"
        string += f":{self.text}"
        return string

class Modes:
    def __init__(self):
        self._modes: Set[str] = set()

    def add(self, mode: GameMode):
        self._modes.add(mode.get_string())

    def remove(self, mode: GameMode):
        self._modes.discard(mode.get_string())

    def clear(self):
        self._modes.clear()

    def __str__(self):
        return ";".join(sorted(self._modes))

    def __eq__(self, other):
        if not isinstance(other, Modes):
            return False
        return self._modes == other._modes

    def __ne__(self, other):
        return not self.__eq__(other)