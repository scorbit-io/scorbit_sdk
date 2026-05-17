# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Scorbit SDK - Python 2.7 wrapper (pure-Python / ctypes).

This package provides a Pythonic interface to the Scorbit SDK C library.
The SDK shared library (``libscorbit_sdk.so`` / ``.dylib`` / ``.dll``) must
be installed separately; see :mod:`scorbit._loader` for discovery rules.

Quick start::

    import scorbit

    config = scorbit.Config()
    config.set_provider("myprovider")
    config.set_machine_id(4419)
    config.set_game_code_version("1.0.0")
    config.set_encrypted_key(encrypted_key)

    def on_event(event):
        print("Event:", event.type)

    config.set_event_callback(on_event)

    with scorbit.create_game_state(config) as gs:
        gs.set_game_started(scorbit.GameStartOrigin.StartButton)
        gs.set_score(1, 42000)
        gs.commit()
"""

from __future__ import absolute_import

from ._version import __version__

# -- public API ---------------------------------------------------------------

from ._enums import (
    AuthStatus,
    Capability,
    Error,
    EventType,
    GameStartOrigin,
    LeaderboardPeriod,
    LeaderboardScope,
    LeaderboardVpinFilter,
    LogLevel,
)
from ._types import (
    BundlePrice,
    LeaderboardEntry,
    LeaderboardPlayer,
    LeaderboardResult,
    PlayerInfo,
    PricingInfo,
)
from .config import Config
from .event import Event
from .game_state import GameState, create_game_state

# -- logger API (only when the native library wires callbacks; spdlog builds omit it).
from ._bindings import _has_logger as _has_logger, _lib as _lib

if _has_logger:
    import traceback as _traceback

    from . import config as _config_mod
    from ._bindings import sb_log_callback_t as _sb_log_callback_t

    _logger_prevent_gc = []  # type: list

    def add_logger_callback(callback, max_length=512):
        # type: (..., int) -> None
        """Register a custom log callback.

        The callback signature is::

            def my_logger(message, level, file, line, timestamp):
                ...

        Args:
            callback: The logger function.
            max_length: Maximum log message length passed to the callback.
        """

        @_sb_log_callback_t
        def _trampoline(message, level, file, line, timestamp, user_data):
            if _config_mod._shutting_down:
                return
            try:
                msg = message
                if isinstance(msg, bytes):
                    msg = msg.decode("utf-8", "replace")
                f = file
                if isinstance(f, bytes):
                    f = f.decode("utf-8", "replace")
                callback(msg, LogLevel(level), f or "", line, timestamp)
            except Exception:
                _traceback.print_exc()

        _logger_prevent_gc.append(_trampoline)
        _lib.sb_add_logger_callback(_trampoline, None, max_length)

    def reset_logger():
        # type: () -> None
        """Remove all previously registered logger callbacks."""
        _lib.sb_reset_logger()
        del _logger_prevent_gc[:]


__all__ = [
    # Version
    "__version__",
    # Enums
    "AuthStatus",
    "Capability",
    "Error",
    "EventType",
    "GameStartOrigin",
    "LeaderboardPeriod",
    "LeaderboardScope",
    "LeaderboardVpinFilter",
    "LogLevel",
    # Types
    "BundlePrice",
    "LeaderboardEntry",
    "LeaderboardPlayer",
    "LeaderboardResult",
    "PlayerInfo",
    "PricingInfo",
    # Classes
    "Config",
    "Event",
    "GameState",
    # Factory
    "create_game_state",
]

if _has_logger:
    __all__ = __all__ + ["add_logger_callback", "reset_logger"]
