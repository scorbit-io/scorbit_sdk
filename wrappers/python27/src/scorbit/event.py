# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Event wrapper -- provides Pythonic access to SDK event data.

An :class:`Event` instance is only valid during the event callback invocation.
Do **not** store it for later use; extract the data you need inside the callback.
"""

from __future__ import absolute_import

from ctypes import POINTER, byref, c_bool, c_char_p, c_int, c_size_t, c_uint, c_uint8

from ._bindings import _lib
from ._enums import EventType
from ._types import PlayerInfo


class Event(object):
    """Wraps a native ``sb_event_t`` pointer received in an event callback.

    Helper methods return the parsed value directly, or ``None`` when the
    event type does not match (instead of the ``(success, value)`` tuple
    used by the pybind11 wrapper).
    """

    __slots__ = ("_ptr",)

    def __init__(self, ptr):
        self._ptr = ptr

    # ------------------------------------------------------------------
    # Core
    # ------------------------------------------------------------------

    @property
    def type(self):
        # type: () -> EventType
        """The event type."""
        return EventType(_lib.sb_event_type(self._ptr))

    # ------------------------------------------------------------------
    # Game start
    # ------------------------------------------------------------------

    def get_game_start_requested(self):
        """Parse a ``GameStartRequested`` event.

        Returns:
            The number of requested players, or ``None`` if the event type
            does not match.
        """
        count = c_int(0)
        if _lib.sb_event_game_start_requested(self._ptr, byref(count)):
            return count.value
        return None

    # ------------------------------------------------------------------
    # Credits
    # ------------------------------------------------------------------

    def get_credits_add_requested(self):
        """Parse a ``CreditsAddRequested`` event.

        Returns:
            A ``(credits, transaction)`` tuple, or ``None`` if the event type
            does not match.
        """
        credits = c_int(0)
        transaction = c_char_p()
        if _lib.sb_event_credits_add_requested(
            self._ptr, byref(credits), byref(transaction)
        ):
            tx_str = transaction.value
            if isinstance(tx_str, bytes):
                tx_str = tx_str.decode("utf-8", "replace")
            return credits.value, (tx_str or "")
        return None

    # ------------------------------------------------------------------
    # Config
    # ------------------------------------------------------------------

    def get_config_payments_enabled(self):
        """Extract the ``payments_enabled`` flag from a ``ConfigReceived`` event.

        Returns:
            ``True`` / ``False``, or ``None`` if the event type does not match.
        """
        enabled = c_bool(False)
        if _lib.sb_event_config_payments_enabled(self._ptr, byref(enabled)):
            return bool(enabled.value)
        return None

    def get_config_received(self):
        """Extract the config JSON string from a ``ConfigReceived`` event.

        Returns:
            The config JSON string, or ``None`` if the event type does not
            match.
        """
        config_json = c_char_p()
        if _lib.sb_event_config_received(self._ptr, byref(config_json)):
            val = config_json.value
            if isinstance(val, bytes):
                val = val.decode("utf-8", "replace")
            return val or ""
        return None

    # ------------------------------------------------------------------
    # Players
    # ------------------------------------------------------------------

    def _get_str(self, func, player):
        out = c_char_p()
        if func(self._ptr, c_uint(player), byref(out)):
            val = out.value
            if isinstance(val, bytes):
                val = val.decode("utf-8", "replace")
            return val or ""
        return ""

    def _build_player_info(self, player):
        # type: (int) -> PlayerInfo
        has = c_bool(False)
        _lib.sb_event_player_has_info(self._ptr, c_uint(player), byref(has))
        return PlayerInfo(
            id=self._get_str(_lib.sb_event_player_id, player),
            preferred_name=self._get_str(_lib.sb_event_player_preferred_name, player),
            name=self._get_str(_lib.sb_event_player_name, player),
            initials=self._get_str(_lib.sb_event_player_initials, player),
            picture_url=self._get_str(_lib.sb_event_player_picture_url, player),
            claim_deeplink=self._get_str(_lib.sb_event_player_claim_deeplink, player),
            has_info=bool(has.value),
        )

    def get_players_updated(self):
        """Parse a ``PlayersUpdated`` event.

        Returns:
            A dict mapping 1-based player numbers to :class:`PlayerInfo`
            objects, or ``None`` if the event type does not match.
        """
        count = c_int(0)
        if not _lib.sb_event_players_updated(self._ptr, byref(count)):
            return None
        players = {}
        for p in range(1, count.value + 1):
            players[p] = self._build_player_info(p)
        return players

    def get_player_picture_ready(self):
        """Parse a ``PlayerPictureReady`` event.

        Returns:
            A ``(player_number, picture_bytes)`` tuple, or ``None`` if the
            event type does not match.
        """
        player = c_uint(0)
        data = POINTER(c_uint8)()
        size = c_size_t(0)
        if _lib.sb_event_player_picture_ready(
            self._ptr, byref(player), byref(data), byref(size)
        ):
            pic = bytes(bytearray(data[: size.value])) if size.value else b""
            return player.value, pic
        return None

    # ------------------------------------------------------------------
    # Internal / scorbitd events
    # ------------------------------------------------------------------

    def get_scorbitd_update_received(self):
        """Extract the update JSON from a ``ScorbitdUpdateReceived`` event."""
        out = c_char_p()
        if _lib.sb_event_scorbitd_update_received(self._ptr, byref(out)):
            val = out.value
            if isinstance(val, bytes):
                val = val.decode("utf-8", "replace")
            return val or ""
        return None

    def get_scorbitd_updated(self):
        """Extract version and executable path from a ``ScorbitdUpdated`` event."""
        version = c_char_p()
        exe_path = c_char_p()
        if _lib.sb_event_scorbitd_updated(
            self._ptr, byref(version), byref(exe_path)
        ):
            v = version.value
            e = exe_path.value
            if isinstance(v, bytes):
                v = v.decode("utf-8", "replace")
            if isinstance(e, bytes):
                e = e.decode("utf-8", "replace")
            return (v or ""), (e or "")
        return None

    def get_firmwares_list_received(self):
        """Extract firmwares list JSON from a ``FirmwaresListReceived`` event."""
        out = c_char_p()
        if _lib.sb_event_firmwares_list_received(self._ptr, byref(out)):
            val = out.value
            if isinstance(val, bytes):
                val = val.decode("utf-8", "replace")
            return val or ""
        return None
