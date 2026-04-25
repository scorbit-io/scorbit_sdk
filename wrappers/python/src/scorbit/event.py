# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Event wrapper — provides Pythonic access to SDK event data.

An :class:`Event` instance is only valid during the event callback invocation.
Do **not** store it for later use; extract the data you need inside the callback.
"""

from ctypes import POINTER, byref, c_bool, c_char_p, c_int, c_size_t, c_uint, c_uint8

from ._bindings import _lib
from ._enums import EventType
from ._types import BundlePrice, PlayerInfo, PricingInfo


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
        # type: () -> int | None
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
        # type: () -> tuple[int, str] | None
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
                tx_str = tx_str.decode("utf-8", errors="replace")
            return credits.value, (tx_str or "")
        return None

    # ------------------------------------------------------------------
    # Pricing
    # ------------------------------------------------------------------

    def _get_pricing_str(self, func):
        # type: (...) -> str
        out = c_char_p()
        if func(self._ptr, byref(out)):
            val = out.value
            if isinstance(val, bytes):
                val = val.decode("utf-8", errors="replace")
            return val or ""
        return ""

    def _get_bundle_str(self, func, index):
        # type: (...) -> str
        out = c_char_p()
        if func(self._ptr, c_int(index), byref(out)):
            val = out.value
            if isinstance(val, bytes):
                val = val.decode("utf-8", errors="replace")
            return val or ""
        return ""

    def get_pricing_received(self):
        # type: () -> PricingInfo | None
        """Parse a ``PricingReceived`` event.

        Returns:
            A :class:`PricingInfo` object, or ``None`` if the event type
            does not match.
        """
        free_play = c_bool(False)
        if not _lib.sb_event_pricing_free_play(self._ptr, byref(free_play)):
            return None

        payments_enabled = c_bool(False)
        _lib.sb_event_pricing_payments_enabled(self._ptr, byref(payments_enabled))

        credit_price = self._get_pricing_str(_lib.sb_event_pricing_credit_price)
        credit_regular = self._get_pricing_str(_lib.sb_event_pricing_credit_regular_price)
        credit_sale = self._get_pricing_str(_lib.sb_event_pricing_credit_sale_price)

        count = c_int(0)
        _lib.sb_event_pricing_bundles_count(self._ptr, byref(count))
        bundles = []
        for i in range(count.value):
            credits = c_int(0)
            _lib.sb_event_pricing_bundle_credits(self._ptr, c_int(i), byref(credits))
            bundles.append(BundlePrice(
                credits=credits.value,
                price=self._get_bundle_str(_lib.sb_event_pricing_bundle_price, i),
                regular_price=self._get_bundle_str(_lib.sb_event_pricing_bundle_regular_price, i),
                sale_price=self._get_bundle_str(_lib.sb_event_pricing_bundle_sale_price, i),
            ))

        return PricingInfo(
            free_play=bool(free_play.value),
            payments_enabled=bool(payments_enabled.value),
            credit_price=credit_price,
            credit_regular_price=credit_regular,
            credit_sale_price=credit_sale,
            bundles=bundles,
        )

    # ------------------------------------------------------------------
    # Config
    # ------------------------------------------------------------------

    def get_config_received(self):
        # type: () -> str | None
        """Extract the config JSON string from a ``ConfigReceived`` event.

        Returns:
            The config JSON string, or ``None`` if the event type does not
            match.
        """
        config_json = c_char_p()
        if _lib.sb_event_config_received(self._ptr, byref(config_json)):
            val = config_json.value
            if isinstance(val, bytes):
                val = val.decode("utf-8", errors="replace")
            return val or ""
        return None

    # ------------------------------------------------------------------
    # Players
    # ------------------------------------------------------------------

    def _get_str(self, func, player):
        # type: (...) -> str
        out = c_char_p()
        if func(self._ptr, c_uint(player), byref(out)):
            val = out.value
            if isinstance(val, bytes):
                val = val.decode("utf-8", errors="replace")
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
        # type: () -> dict[int, PlayerInfo] | None
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
        # type: () -> tuple[int, bytes] | None
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
        # type: () -> str | None
        """Extract the update JSON from a ``ScorbitdUpdateReceived`` event."""
        out = c_char_p()
        if _lib.sb_event_scorbitd_update_received(self._ptr, byref(out)):
            val = out.value
            if isinstance(val, bytes):
                val = val.decode("utf-8", errors="replace")
            return val or ""
        return None

    def get_scorbitd_updated(self):
        # type: () -> tuple[str, str] | None
        """Extract version and executable path from a ``ScorbitdUpdated`` event."""
        version = c_char_p()
        exe_path = c_char_p()
        if _lib.sb_event_scorbitd_updated(
            self._ptr, byref(version), byref(exe_path)
        ):
            v = version.value
            e = exe_path.value
            if isinstance(v, bytes):
                v = v.decode("utf-8", errors="replace")
            if isinstance(e, bytes):
                e = e.decode("utf-8", errors="replace")
            return (v or ""), (e or "")
        return None

    def get_firmwares_list_received(self):
        # type: () -> str | None
        """Extract firmwares list JSON from a ``FirmwaresListReceived`` event."""
        out = c_char_p()
        if _lib.sb_event_firmwares_list_received(self._ptr, byref(out)):
            val = out.value
            if isinstance(val, bytes):
                val = val.decode("utf-8", errors="replace")
            return val or ""
        return None

    # ------------------------------------------------------------------
    # Diagnostics
    # ------------------------------------------------------------------

    def get_diagnostics_upload_requested(self):
        # type: () -> bool | None
        """Parse a ``DiagnosticsUploadRequested`` event.

        Returns:
            ``True`` if recordings should be included, ``False`` otherwise,
            or ``None`` if the event type does not match.
        """
        include_recordings = c_bool(False)
        if _lib.sb_event_diagnostics_upload_requested(
            self._ptr, byref(include_recordings)
        ):
            return bool(include_recordings.value)
        return None

    def get_diagnostics_uploaded(self):
        # type: () -> bool | None
        """Parse a ``DiagnosticsUploaded`` event.

        Returns:
            ``True`` if the upload succeeded, ``False`` otherwise,
            or ``None`` if the event type does not match.
        """
        success = c_bool(False)
        if _lib.sb_event_diagnostics_uploaded(self._ptr, byref(success)):
            return bool(success.value)
        return None
