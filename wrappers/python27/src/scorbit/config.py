# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""ABI-stable configuration wrapper.

Create a :class:`Config`, set its properties, then pass it to
:func:`scorbit.create_game_state`.  The config may be destroyed (or garbage
collected) after the game state has been created.
"""

from __future__ import absolute_import

import atexit
import sys
import traceback
from ctypes import POINTER, c_char_p, c_size_t, c_uint8, c_void_p

from ._bindings import (
    SB_DIGEST_LENGTH,
    SB_SIGNATURE_MAX_LENGTH,
    _lib,
    sb_event_callback_t,
    sb_load_key_callback_t,
    sb_save_key_callback_t,
    sb_signer_callback_t,
)
from .event import Event

_shutting_down = False


def _on_shutdown():
    global _shutting_down
    _shutting_down = True


atexit.register(_on_shutdown)

# POINTER(c_char) on Py2 requires 1-char str
_NUL_C_CHAR = "\x00"

try:
    _text_types = (str, unicode)
except NameError:
    _text_types = (str,)


def _encode(s):
    # type: (str | None) -> bytes | None
    """Encode a Python string to UTF-8 bytes for C, or return None."""
    if s is None:
        return None
    if isinstance(s, bytes):
        return s
    if isinstance(s, _text_types):
        return s.encode("utf-8")
    return s


class Config(object):
    """ABI-stable configuration for creating a game state.

    Example::

        config = scorbit.Config()
        config.set_provider("myprovider")
        config.set_machine_id(4419)
        config.set_game_code_version("1.0.0")
        config.set_encrypted_key(encrypted_key)
        config.set_event_callback(on_event)
        gs = scorbit.create_game_state(config)
    """

    def __init__(self):
        # type: () -> None
        self._handle = _lib.sb_config_create()
        self._prevent_gc = []  # type: list

    @property
    def _as_parameter_(self):
        """Allow passing a Config directly to ctypes functions."""
        return self._handle

    def destroy(self):
        # type: () -> None
        """Free the underlying C config object.

        Called automatically when the Config is garbage collected, but may
        be called explicitly if desired.
        """
        if self._handle:
            _lib.sb_config_destroy(self._handle)
            self._handle = None

    def __del__(self):
        self.destroy()

    # ------------------------------------------------------------------
    # Required settings
    # ------------------------------------------------------------------

    def set_provider(self, provider):
        # type: (str) -> Config
        """Set the provider name (mandatory).

        Args:
            provider: e.g. ``"scorbitron"``, ``"vpin"``.
        """
        _lib.sb_config_set_provider(self._handle, _encode(provider))
        return self

    def set_machine_id(self, machine_id):
        # type: (int) -> Config
        """Set the Scorbit Machine ID (mandatory for manufacturers).

        Args:
            machine_id: e.g. ``4419``.
        """
        _lib.sb_config_set_machine_id(self._handle, machine_id)
        return self

    def set_game_code_version(self, version):
        # type: (str) -> Config
        """Set the game code version (mandatory).

        Args:
            version: e.g. ``"1.12.3"``.
        """
        _lib.sb_config_set_game_code_version(self._handle, _encode(version))
        return self

    # ------------------------------------------------------------------
    # Optional settings
    # ------------------------------------------------------------------

    def set_hostname(self, hostname):
        # type: (str) -> Config
        """Set the server hostname.

        Args:
            hostname: ``"production"`` (default), ``"staging"``, or a full URL.
        """
        _lib.sb_config_set_hostname(self._handle, _encode(hostname))
        return self

    def set_cf_hostname(self, hostname):
        # type: (str) -> Config
        """Set the Centrifugo server hostname."""
        _lib.sb_config_set_cf_hostname(self._handle, _encode(hostname))
        return self

    def set_uuid(self, uuid):
        # type: (str) -> Config
        """Set the device UUID.

        If not set, the UUID is derived from the device's MAC address.
        """
        _lib.sb_config_set_uuid(self._handle, _encode(uuid))
        return self

    def set_serial_number(self, serial_number):
        # type: (int) -> Config
        """Set the device serial number. Set to ``0`` if unavailable."""
        _lib.sb_config_set_serial_number(self._handle, serial_number)
        return self

    def set_auto_download_player_pics(self, enable):
        # type: (bool) -> Config
        """Enable or disable automatic player picture downloads."""
        _lib.sb_config_set_auto_download_player_pics(self._handle, enable)
        return self

    def set_score_features(self, features, version=1):
        # type: (list, int) -> Config
        """Set score features that identify what triggered a score increase.

        Args:
            features: List of feature names, e.g.
                ``["ramp", "left spinner", "right spinner"]``.
            version: Increment when new features are added in future releases.
        """
        encoded = [_encode(f) for f in features]
        arr = (c_char_p * len(encoded))(*encoded)
        _lib.sb_config_set_score_features(
            self._handle, arr, len(encoded), version
        )
        return self

    # ------------------------------------------------------------------
    # Authentication
    # ------------------------------------------------------------------

    def set_encrypted_key(self, encrypted_key):
        # type: (str) -> Config
        """Set the encrypted key for authentication (non-TPM machines).

        The key is generated with the ``encrypt_tool`` provided with the SDK.
        Either this or :meth:`set_signer` must be called before creating the
        game state.

        Args:
            encrypted_key: The encrypted private key string.
        """
        _lib.sb_config_set_encrypted_key(self._handle, _encode(encrypted_key))
        return self

    def set_signer(self, callback):
        # type: (...) -> Config
        """Set a TPM signer callback for authentication.

        The callback signature must be::

            def signer(digest: bytes) -> bytes:
                ...

        where *digest* is a 32-byte SHA-256 hash and the return value is the
        ECDSA P-256 signature (up to 72 bytes).

        Either this or :meth:`set_encrypted_key` must be called before
        creating the game state.
        """

        @sb_signer_callback_t
        def _trampoline(sig_buf, sig_len_ptr, digest_ptr, user_data):
            if _shutting_down:
                return -1
            try:
                digest_bytes = bytes(bytearray(digest_ptr[:SB_DIGEST_LENGTH]))
                sig = callback(digest_bytes)
                if not isinstance(sig, (bytes, bytearray)):
                    return -1
                length = min(len(sig), SB_SIGNATURE_MAX_LENGTH)
                for i in range(length):
                    sig_buf[i] = sig[i]
                sig_len_ptr[0] = length
                return 0
            except Exception:
                traceback.print_exc()
                return -1

        self._prevent_gc.append(_trampoline)
        _lib.sb_config_set_signer(self._handle, _trampoline, None)
        return self

    # ------------------------------------------------------------------
    # Event callback
    # ------------------------------------------------------------------

    def set_event_callback(self, callback):
        # type: (...) -> Config
        """Set the callback invoked when SDK events occur.

        The callback receives a single :class:`~scorbit.Event` argument::

            def on_event(event):
                if event.type == scorbit.EventType.GameStartRequested:
                    players = event.get_game_start_requested()
                    ...

        Note:
            The callback is invoked from a background thread.  The GIL is
            acquired automatically by ctypes, but you should use a lock if
            you share mutable state with the main thread.

            This must be set **before** creating the game state.
        """

        @sb_event_callback_t
        def _trampoline(event_ptr, user_data):
            if _shutting_down:
                return
            try:
                callback(Event(event_ptr))
            except Exception:
                traceback.print_exc()

        self._prevent_gc.append(_trampoline)
        _lib.sb_config_set_event_callback(self._handle, _trampoline, None)
        return self

    # ------------------------------------------------------------------
    # Key persistence callbacks
    # ------------------------------------------------------------------

    def set_save_key_callback(self, callback):
        # type: (...) -> Config
        """Set a callback the SDK calls when it needs to save a key.

        Callback signature::

            def save_key(key):
                ...

        Args:
            callback: Receives the key string to persist.
        """

        @sb_save_key_callback_t
        def _trampoline(key, user_data):
            if _shutting_down:
                return
            try:
                val = key
                if isinstance(val, bytes):
                    val = val.decode("utf-8", "replace")
                callback(val)
            except Exception:
                traceback.print_exc()

        self._prevent_gc.append(_trampoline)
        _lib.sb_config_set_save_key_callback(self._handle, _trampoline, None)
        return self

    def set_load_key_callback(self, callback):
        # type: (...) -> Config
        """Set a callback the SDK calls when it needs to load a saved key.

        Callback signature::

            def load_key():
                ...

        Return the key string, or an empty string / ``None`` if no key is
        stored.

        Args:
            callback: Called with no arguments; returns the key string.
        """

        @sb_load_key_callback_t
        def _trampoline(buffer, buffer_size, user_data):
            if _shutting_down:
                return 0
            try:
                result = callback()
                if result is None:
                    return 0
                encoded = _encode(result)
                if not encoded:
                    return 0
                length = len(encoded)
                if length + 1 > buffer_size:
                    return -1
                for i in range(length):
                    buffer[i] = encoded[i]
                buffer[length] = _NUL_C_CHAR
                return length
            except Exception:
                traceback.print_exc()
                return 0

        self._prevent_gc.append(_trampoline)
        _lib.sb_config_set_load_key_callback(self._handle, _trampoline, None)
        return self

    # ------------------------------------------------------------------
    # Internal / scorbitd
    # ------------------------------------------------------------------

    def set_scorbitd_version(self, version):
        # type: (str) -> Config
        _lib.sb_config_set_scorbitd_version(self._handle, _encode(version))
        return self

    def set_scorbitd_platform_id(self, platform_id):
        # type: (str) -> Config
        _lib.sb_config_set_scorbitd_platform_id(self._handle, _encode(platform_id))
        return self

    def set_machine_title(self, title):
        # type: (str) -> Config
        _lib.sb_config_set_machine_title(self._handle, _encode(title))
        return self

    def set_extra_fingerprint(self, extra):
        # type: (str) -> Config
        _lib.sb_config_set_extra_fingerprint(self._handle, _encode(extra))
        return self
