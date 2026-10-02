# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Game state wrapper - the main entry point for interacting with the SDK.

Use :func:`create_game_state` to obtain a :class:`GameState` instance.
The object supports the context-manager protocol for automatic cleanup::

    with scorbit.create_game_state(config) as gs:
        gs.set_game_started(scorbit.GameStartOrigin.StartButton)
        gs.set_score(1, 42000)
        gs.commit()
"""

import traceback
from ctypes import (
    POINTER,
    byref,
    c_bool,
    c_char_p,
    c_int,
    c_int64,
    c_size_t,
    c_uint8,
    c_uint64,
    string_at,
)

from ._bindings import (
    _lib,
    sb_buffer_callback_t,
    sb_http_status_callback_t,
    sb_leaderboard_callback_t,
    sb_achievement_progress_t,
    sb_achievement_rule_progress_t,
    sb_achievement_rule_t,
    sb_achievement_t,
    sb_string_callback_t,
)
from ._enums import (
    AchievementComparison,
    AchievementEvaluation,
    AchievementRuleType,
    AchievementScope,
    AuthStatus,
    Error,
    GameStartOrigin,
    LeaderboardPeriod,
    LeaderboardScope,
    LeaderboardVpinFilter,
)
from . import config as _config_mod
from .config import Config, _encode
from ._types import (
    Achievement,
    AchievementProgress,
    AchievementRule,
    AchievementRuleProgress,
    LeaderboardEntry,
    LeaderboardPlayer,
    LeaderboardResult,
)


class GameState(object):
    """Wraps the native ``sb_game_handle_t`` opaque handle.

    Modification methods (``set_score``, ``add_mode``, etc.) stage changes
    locally.  Call :meth:`commit` to push them to the cloud.

    Warning:
        If the game is not active, all modification calls and ``commit()``
        are silently ignored by the C SDK.
    """

    def __init__(self, handle, config):
        # type: (int, Config) -> None
        self._handle = handle
        # Keep config callbacks alive for the lifetime of the game state
        self._config = config
        # Async callback trampolines that must not be GC'd while in-flight
        self._async_callbacks = []  # type: list

    # ------------------------------------------------------------------
    # Context manager
    # ------------------------------------------------------------------

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.destroy()
        return False

    # ------------------------------------------------------------------
    # Lifecycle
    # ------------------------------------------------------------------

    def destroy(self):
        # type: () -> None
        """Destroy the game state and release all resources.

        Safe to call multiple times.  Also called automatically when used
        as a context manager or when garbage collected.
        """
        if self._handle:
            _lib.sb_destroy_game_state(self._handle)
            self._handle = None

    def __del__(self):
        self.destroy()

    # ------------------------------------------------------------------
    # Game flow
    # ------------------------------------------------------------------

    def set_game_started(self, origin):
        # type: (GameStartOrigin | int) -> None
        """Mark the game as started.

        Resets the game state: active player becomes Player 1 with score 0,
        current ball is set to 1.  Has no effect if a game is already active.

        After calling this, you must call :meth:`commit` to notify the cloud.

        Args:
            origin: How the game was started.  See :class:`GameStartOrigin`.
        """
        _lib.sb_set_game_started(self._handle, int(origin))

    def set_game_finished(self):
        # type: () -> None
        """Mark the game as finished.

        This automatically commits.  No further score/mode changes are
        accepted after this call.
        """
        _lib.sb_set_game_finished(self._handle)

    def set_current_ball(self, ball):
        # type: (int) -> None
        """Set the current ball number (1--9)."""
        _lib.sb_set_current_ball(self._handle, ball)

    def set_active_player(self, player):
        # type: (int) -> None
        """Set the active player (1--9).

        If the player does not yet exist, it is created with score 0.
        """
        _lib.sb_set_active_player(self._handle, player)

    def set_score(self, player, score, feature=0):
        # type: (int, int, int) -> None
        """Set a player's score.

        Args:
            player: Player number (1--9).
            score: The new score.
            feature: Optional score-feature index (0 = none).
        """
        _lib.sb_set_score(self._handle, player, score, feature)

    # ------------------------------------------------------------------
    # Modes
    # ------------------------------------------------------------------

    def add_mode(self, mode):
        # type: (str) -> None
        """Add a mode to the active mode list.

        Args:
            mode: e.g. ``"MB:Multiball"``.  Duplicates are ignored.
        """
        _lib.sb_add_mode(self._handle, _encode(mode))

    def add_mode_expiring(self, mode, duration_seconds=3):
        # type: (str, int) -> None
        """Add a mode that auto-expires after *duration_seconds*.

        Duration rules: ``0`` is normalised to ``3``; values above ``10``
        are clamped to ``10``.  The recommended default is ``3``.
        """
        _lib.sb_add_mode_expiring(self._handle, _encode(mode), duration_seconds)

    def set_mode_completed(self, mode):
        # type: (str) -> None
        """Mark a mode as completed (achieved, finished, beaten).

        Unlike :meth:`add_mode`, which tells what is happening right now,
        this reports a one-shot event: what the player accomplished.  It is
        meant to be used by achievements.

        A completed mode is not a regular mode: it is not added to the
        active mode list, it is not affected by :meth:`remove_mode` or
        :meth:`clear_modes`, and it is not published as a current game
        mode.  It is recorded in the game session history, in the
        ``completed_modes`` column, next to the ``game_modes`` column which
        holds the active modes.

        Being an event and not a state, it is reported only in the update
        which follows the call and is not repeated in the subsequent ones.
        Marking a mode as completed makes the game state changed, so the
        next :meth:`commit` sends an update even if nothing else changed.

        Args:
            mode: e.g. ``"NA:The Tale of the Forty Thieves"``.
        """
        _lib.sb_set_mode_completed(self._handle, _encode(mode))

    def add_event(self, name, value):
        # type: (str, int) -> None
        """Add *value* to the named event register.

        An event is a named 64-bit counter the game accumulates (spinner
        spins, ramps, ...), matched against achievement conditions.  Like a
        completed mode it is one-shot: it is recorded only in the history
        row produced by the next :meth:`commit`.

        Args:
            name: e.g. ``"spins"``.  Must not be empty or contain ``;``,
                ``=``, ``,``, ``"`` or a newline; such a call is ignored.
            value: amount to add; may be negative.
        """
        _lib.sb_add_event(self._handle, _encode(name), value)

    def remove_mode(self, mode):
        # type: (str) -> None
        """Remove a mode from the active list (no-op if absent)."""
        _lib.sb_remove_mode(self._handle, _encode(mode))

    def clear_modes(self):
        # type: () -> None
        """Remove all modes."""
        _lib.sb_clear_modes(self._handle)

    # ------------------------------------------------------------------
    # Commit
    # ------------------------------------------------------------------

    def commit(self):
        # type: () -> None
        """Push all staged changes to the cloud.

        Call this at the end of each game-loop cycle.  If nothing has
        changed since the last commit, this is a no-op.
        """
        _lib.sb_commit(self._handle)

    # ------------------------------------------------------------------
    # Status (properties)
    # ------------------------------------------------------------------

    @property
    def status(self):
        # type: () -> AuthStatus
        """Current authentication / pairing status."""
        return AuthStatus(_lib.sb_get_status(self._handle))

    @property
    def machine_uuid(self):
        # type: () -> str
        """The machine UUID (derived from MAC if not set explicitly)."""
        raw = _lib.sb_get_machine_uuid(self._handle)
        if raw and isinstance(raw, bytes):
            return raw.decode("utf-8", errors="replace")
        return raw or ""

    @property
    def machine_serial(self):
        # type: () -> int
        """The machine serial number (uint64, same as TPM / device info)."""
        return _lib.sb_get_machine_serial(self._handle)

    @property
    def pair_deeplink(self):
        # type: () -> str
        """Pairing deeplink URL (empty if not yet authenticated/paired)."""
        raw = _lib.sb_get_pair_deeplink(self._handle)
        if raw and isinstance(raw, bytes):
            return raw.decode("utf-8", errors="replace")
        return raw or ""

    # ------------------------------------------------------------------
    # Async requests with callbacks
    # ------------------------------------------------------------------

    def _make_string_cb(self, callback):
        """Wrap a Python ``(error, reply)`` callback in a C trampoline."""

        @sb_string_callback_t
        def _trampoline(error_code, reply, user_data):
            if _config_mod._shutting_down:
                return
            try:
                reply_str = reply
                if isinstance(reply_str, bytes):
                    reply_str = reply_str.decode("utf-8", errors="replace")
                callback(Error(error_code), reply_str or "")
            except Exception:
                traceback.print_exc()

        self._async_callbacks.append(_trampoline)
        return _trampoline

    def _make_http_status_cb(self, callback):
        """Wrap a Python ``(error, http_status, reply)`` callback in a C trampoline."""

        @sb_http_status_callback_t
        def _trampoline(error_code, http_status, reply, user_data):
            if _config_mod._shutting_down:
                return
            try:
                reply_str = reply
                if isinstance(reply_str, bytes):
                    reply_str = reply_str.decode("utf-8", errors="replace")
                callback(Error(error_code), http_status, reply_str or "")
            except Exception:
                traceback.print_exc()

        self._async_callbacks.append(_trampoline)
        return _trampoline

    def _make_buffer_cb(self, callback):
        """Wrap a Python ``(error, data_bytes)`` callback in a C trampoline."""

        @sb_buffer_callback_t
        def _trampoline(error_code, data, size, user_data):
            if _config_mod._shutting_down:
                return
            try:
                buf = bytes(bytearray(data[:size])) if size else b""
                callback(Error(error_code), buf)
            except Exception:
                traceback.print_exc()

        self._async_callbacks.append(_trampoline)
        return _trampoline

    @staticmethod
    def _decode_c_string(raw):
        # type: (bytes | str | None) -> str
        if isinstance(raw, bytes):
            return raw.decode("utf-8", errors="replace")
        return raw or ""

    def _leaderboard_from_handle(self, leaderboard):
        # type: (int) -> LeaderboardResult
        count = int(_lib.sb_leaderboard_entries_count(leaderboard))
        entries = []

        for index in range(count):
            player = LeaderboardPlayer()
            entry = LeaderboardEntry(player=player)

            value_u64 = c_uint64()
            value_i64 = c_int64()
            value_i = c_int()
            value_b = c_bool()
            value_s = c_char_p()

            if _lib.sb_leaderboard_entry_id(leaderboard, index, byref(value_u64)):
                entry.id = int(value_u64.value)
            if _lib.sb_leaderboard_entry_rank(leaderboard, index, byref(value_i)):
                entry.rank = int(value_i.value)
            if _lib.sb_leaderboard_entry_high_score(leaderboard, index, byref(value_i64)):
                entry.high_score = int(value_i64.value)
            if _lib.sb_leaderboard_entry_image(leaderboard, index, byref(value_s)):
                entry.image = self._decode_c_string(value_s.value)
            if _lib.sb_leaderboard_entry_reaction_count(leaderboard, index, byref(value_i)):
                entry.reaction_count = int(value_i.value)
            if _lib.sb_leaderboard_entry_score_count(leaderboard, index, byref(value_i)):
                entry.score_count = int(value_i.value)
            if _lib.sb_leaderboard_entry_is_nfc_verified(leaderboard, index, byref(value_b)):
                entry.is_nfc_verified = bool(value_b.value)
            if _lib.sb_leaderboard_entry_is_verified(leaderboard, index, byref(value_b)):
                entry.is_verified = bool(value_b.value)
            if _lib.sb_leaderboard_entry_is_vpin(leaderboard, index, byref(value_b)):
                entry.is_vpin = bool(value_b.value)
            if _lib.sb_leaderboard_entry_created(leaderboard, index, byref(value_s)):
                entry.created = self._decode_c_string(value_s.value)

            if _lib.sb_leaderboard_entry_player_id(leaderboard, index, byref(value_s)):
                player.id = self._decode_c_string(value_s.value)
            if _lib.sb_leaderboard_entry_player_username(leaderboard, index, byref(value_s)):
                player.username = self._decode_c_string(value_s.value)
            if _lib.sb_leaderboard_entry_player_display_name(leaderboard, index, byref(value_s)):
                player.display_name = self._decode_c_string(value_s.value)
            if _lib.sb_leaderboard_entry_player_initials(leaderboard, index, byref(value_s)):
                player.initials = self._decode_c_string(value_s.value)
            if _lib.sb_leaderboard_entry_player_avatar(leaderboard, index, byref(value_s)):
                player.avatar = self._decode_c_string(value_s.value)
            if _lib.sb_leaderboard_entry_player_follower_count(leaderboard, index, byref(value_i)):
                player.follower_count = int(value_i.value)
            if _lib.sb_leaderboard_entry_player_following_count(leaderboard, index, byref(value_i)):
                player.following_count = int(value_i.value)
            if _lib.sb_leaderboard_entry_player_last_login(leaderboard, index, byref(value_s)):
                player.last_login = self._decode_c_string(value_s.value)

            entries.append(entry)

        return LeaderboardResult(entries)

    def _make_leaderboard_cb(self, callback):
        """Wrap a Python ``(error, leaderboard)`` callback in a C trampoline."""

        @sb_leaderboard_callback_t
        def _trampoline(error_code, leaderboard, user_data):
            try:
                if _config_mod._shutting_down:
                    return
                result = (
                    self._leaderboard_from_handle(leaderboard)
                    if leaderboard
                    else LeaderboardResult()
                )
                callback(Error(error_code), result)
            except Exception:
                traceback.print_exc()

        self._async_callbacks.append(_trampoline)
        return _trampoline

    def request_top_scores(self, scope, period, since, vpin_filter, callback):
        # type: (LeaderboardScope | int, LeaderboardPeriod | int, str | None, LeaderboardVpinFilter | int, ...) -> None
        """Fetch leaderboard scores asynchronously.

        Args:
            scope: :class:`LeaderboardScope` or integer value selecting
                machine vs variant vs game leaderboard.
            period: :class:`LeaderboardPeriod` or integer value selecting
                the backend time bucket.
            since: Optional UTC ISO-8601 lower-bound time filter. When set,
                the backend ignores *period*.
            vpin_filter: :class:`LeaderboardVpinFilter` or integer value
                selecting whether virtual pinball scores are included.
            callback: ``(error: Error, leaderboard: LeaderboardResult) -> None``.
        """
        cb = self._make_leaderboard_cb(callback)
        _lib.sb_request_top_scores(
            self._handle,
            int(scope),
            int(period),
            _encode(since) if since else None,
            int(vpin_filter),
            cb,
            None,
        )

    def request_pair_code(self, callback):
        # type: (...) -> None
        """Request a 6-character pairing short code.

        Args:
            callback: ``(error: Error, code: str) -> None``.
        """
        cb = self._make_string_cb(callback)
        _lib.sb_request_pair_code(self._handle, cb, None)

    def request_unpair(self, callback):
        # type: (...) -> None
        """Request to unpair the device.

        Args:
            callback: ``(error: Error, reply: str) -> None``.
        """
        cb = self._make_string_cb(callback)
        _lib.sb_request_unpair(self._handle, cb, None)

    # ------------------------------------------------------------------
    # Achievements
    # ------------------------------------------------------------------

    def _achievement_from_c(self, c):
        # type: (...) -> Achievement
        decode = self._decode_c_string
        a = Achievement()
        a.key = decode(c.key)
        a.name = decode(c.name)
        a.description = decode(c.description)
        a.scope = AchievementScope(c.scope)
        a.evaluation = AchievementEvaluation(c.evaluation)
        a.is_trophy = bool(c.is_trophy)
        a.visible = bool(c.visible)
        a.obscure = bool(c.obscure)
        a.notify_when_achieved = bool(c.notify_when_achieved)
        a.icon_url = decode(c.icon_url)
        a.obscure_image_url = decode(c.obscure_image_url)
        if c.has_group:
            a.group_id = int(c.group_id)
            a.display_position = int(c.display_position)

        # Copied first: the next call may invalidate the definition's strings
        key = _encode(a.key)
        for i in range(int(c.rules_count)):
            rule = sb_achievement_rule_t()
            if _lib.sb_achievement_rule_at(self._handle, key, i, byref(rule)):
                a.rules.append(
                    AchievementRule(
                        AchievementRuleType(rule.type),
                        AchievementComparison(rule.comparison),
                        int(rule.target),
                        decode(rule.reference),
                    )
                )
        return a

    def get_achievements(self):
        # type: () -> list
        """Every achievement definition cached for this machine.

        Achievements are evaluated automatically on every :meth:`commit`
        and reported through the event callback as
        ``EventType.AchievementUpdated``.
        """
        result = []
        for i in range(_lib.sb_achievements_count(self._handle)):
            c = sb_achievement_t()
            if _lib.sb_achievement_at(self._handle, i, byref(c)):
                result.append(self._achievement_from_c(c))
        return result

    def find_achievement(self, key):
        # type: (str) -> Achievement | None
        """The achievement definition with *key*, or ``None``."""
        c = sb_achievement_t()
        if not _lib.sb_achievement_find(self._handle, _encode(key), byref(c)):
            return None
        return self._achievement_from_c(c)

    def get_achievement_progress(self, player, key):
        # type: (int, str) -> AchievementProgress | None
        """*player*'s progress on achievement *key* during the current game.

        Returns ``None`` when the slot is not claimed or the achievement is
        not tracked for the player.
        """
        definition = sb_achievement_t()
        progress = sb_achievement_progress_t()
        encoded = _encode(key)
        if not _lib.sb_achievement_find(self._handle, encoded, byref(definition)):
            return None
        if not _lib.sb_achievement_player_progress(self._handle, player, encoded, byref(progress)):
            return None

        rules = []
        for i in range(int(definition.rules_count)):
            rule = sb_achievement_rule_progress_t()
            if _lib.sb_achievement_player_rule_progress(
                self._handle, player, encoded, i, byref(rule)
            ):
                rules.append(
                    AchievementRuleProgress(
                        bool(rule.judged), int(rule.value), int(rule.target), bool(rule.satisfied)
                    )
                )
            else:
                rules.append(AchievementRuleProgress())
        return AchievementProgress(
            bool(progress.held), bool(progress.all_satisfied), bool(progress.confirmed), rules
        )

    def refresh_achievements(self):
        # type: () -> None
        """Revalidate the cached achievement definitions now."""
        _lib.sb_refresh_achievements(self._handle)

    def fetch_player_achievements(self, user_id, callback):
        # type: (str, ...) -> None
        """Fetch a player's stored achievements state as JSON.

        Args:
            user_id: The player's user id.
            callback: ``(error: Error, reply: str) -> None``.
        """
        cb = self._make_string_cb(callback)
        _lib.sb_fetch_player_achievements(self._handle, _encode(user_id), cb, None)

    def flush_achievement_reports(self):
        # type: () -> None
        """Report changed achievement progress now instead of at ball end."""
        _lib.sb_flush_achievement_reports(self._handle)

    def download_achievement_frames(self):
        # type: () -> None
        """Download the achievement DMD frames that changed on the server."""
        _lib.sb_download_achievement_frames(self._handle)

    def get_achievement_frame(self, key):
        # type: (str) -> bytes | None
        """The DMD frame of achievement *key*, or ``None``."""
        data = POINTER(c_uint8)()
        size = c_size_t(0)
        if not _lib.sb_achievement_frame(self._handle, _encode(key), byref(data), byref(size)):
            return None
        return string_at(data, size.value)

    # ------------------------------------------------------------------
    # Capabilities / Credits
    # ------------------------------------------------------------------

    def set_capabilities(self, capabilities):
        # type: (int) -> None
        """Set device capabilities (bitwise OR of :class:`Capability` flags)."""
        _lib.sb_set_capabilities(self._handle, capabilities)

    def set_credits_dropped(self, credits, transaction, success):
        # type: (int, str, bool) -> None
        """Notify the cloud that credits were dropped.

        Call this after handling a ``CreditsAddRequested`` event.

        Args:
            credits: Number of credits dropped.
            transaction: Transaction ID from the event.
            success: Whether the drop succeeded.
        """
        _lib.sb_set_credits_dropped(
            self._handle, credits, _encode(transaction), success
        )

    def set_credits_status(self, free_play, credits, max_credits, pricing=""):
        # type: (bool, int, int, str) -> None
        """Report current credits status.

        Call when ``CreditsStatusRequested`` is received or whenever the
        credit count changes.

        Args:
            free_play: True if the machine is in free-play mode.
            credits: Current credit count.
            max_credits: Maximum credits the machine accepts.
            pricing: Reserved for future use (pass ``""``).
        """
        _lib.sb_set_credits_status(
            self._handle, free_play, credits, max_credits, _encode(pricing)
        )

    # ------------------------------------------------------------------
    # Downloads
    # ------------------------------------------------------------------

    def download(self, url, filename, content_type, callback):
        # type: (str, str, str, ...) -> None
        """Download a file to local storage asynchronously.

        Args:
            url: Source URL.
            filename: Local path to save to.
            content_type: HTTP ``Accept`` content type (empty for default).
            callback: ``(error: Error, result: str) -> None``.
        """
        cb = self._make_string_cb(callback)
        _lib.sb_download(
            self._handle,
            _encode(url),
            _encode(filename),
            _encode(content_type),
            cb,
            None,
        )

    def download_buffer(self, url, reserve_buffer_size, content_type, callback):
        # type: (str, int, str, ...) -> None
        """Download data into memory asynchronously.

        Args:
            url: Source URL.
            reserve_buffer_size: Initial buffer size hint.
            content_type: HTTP ``Accept`` content type (empty for default).
            callback: ``(error: Error, data: bytes) -> None``.
        """
        cb = self._make_buffer_cb(callback)
        _lib.sb_download_buffer(
            self._handle,
            _encode(url),
            reserve_buffer_size,
            _encode(content_type),
            cb,
            None,
        )

    # ------------------------------------------------------------------
    # Diagnostics
    # ------------------------------------------------------------------

    def upload_diagnostics(self, log_paths=None, recording_paths=None, log_string="",
                           request_generation=None):
        # type: (list[str] | None, list[str] | None, str, int | None) -> None
        """Upload diagnostics (logs, recordings, arbitrary text) to the API.

        The SDK enforces limits: max 5 log files (each <= 10 MB), max 2
        recordings (each <= 20 MB), log string truncated to 10 MB.

        When the upload completes, a ``DiagnosticsUploaded`` event is fired.

        Args:
            log_paths: List of file paths to log files.
            recording_paths: List of file paths to recording files.
            log_string: Arbitrary log text to include.
            request_generation: The generation this upload answers, or None when
                there is no such request. When None the field is omitted entirely
                -- the SDK never invents a value.
        """
        log_list = log_paths or []
        rec_list = recording_paths or []

        log_arr = (c_char_p * len(log_list))(
            *[_encode(p) for p in log_list]
        ) if log_list else None
        rec_arr = (c_char_p * len(rec_list))(
            *[_encode(p) for p in rec_list]
        ) if rec_list else None

        gen_ptr = byref(c_uint64(request_generation)) \
            if request_generation is not None else None

        _lib.sb_upload_diagnostics_ex(
            self._handle,
            log_arr, len(log_list),
            rec_arr, len(rec_list),
            _encode(log_string or ""),
            gen_ptr,
        )

    def report_device_state(self, type, version, installed=True, log=None, callback=None):
        # type: (str, str, bool, str, ...) -> None
        """Report a typed piece of device state to the API.

        The SDK does not interpret ``type`` or ``version``: any type the API
        understands can be reported, and ``version`` is sent verbatim as a JSON
        string. A blank ``version`` is sent as ``"version": ""`` rather than
        omitted -- that is how a caller withdraws an earlier report.

        The SDK does not retry a 4xx or 5xx, with one exception: a 401 is
        answered by re-authenticating and trying again, because the SDK owns
        authentication. Callers must not add a retry of their own; a re-send
        should be a fresh report of current state, not a retry of a failed
        message.

        Args:
            type: The update type, e.g. ``"sdk"``. Passed through unchanged.
            version: The version being reported. Empty withdraws a prior report.
            installed: Whether the reported item is installed.
            log: Optional log text to attach, or None to omit the field.
            callback: ``(error: Error, http_status: int, reply: str) -> None``.
                ``http_status`` is 0 when no HTTP response was received.
        """
        cb = self._make_http_status_cb(callback) if callback else sb_http_status_callback_t()
        _lib.sb_report_device_state(
            self._handle,
            _encode(type),
            _encode(version),
            bool(installed),
            _encode(log) if log is not None else None,
            cb,
            None,
        )

    # ------------------------------------------------------------------
    # Internal / scorbitd
    # ------------------------------------------------------------------

    def request_pair_machine(self, machine_uuid, owner_uuid, callback):
        # type: (str, str, ...) -> None
        cb = self._make_string_cb(callback)
        _lib.sb_game_request_pair_machine(
            self._handle, _encode(machine_uuid), _encode(owner_uuid), cb, None
        )

    def submit_hardware_probe_result(self, run_id, result_json, callback=None):
        # type: (str, str, ...) -> None
        """POST a hardware probe result (serialized JSON object, sent verbatim).

        Nothing is sent for an invalid ``run_id`` or a ``result_json`` that is
        not a JSON object; the callback then gets ``Error.Unknown`` and status 0.
        The SDK retries only transport failures and a 401; any further retry
        policy is the caller's, and the API accepts an identical resubmission.

        Args:
            run_id: The run id from ``Event.get_hardware_probe_requested``.
            result_json: The serialized result object.
            callback: ``(error: Error, http_status: int, reply: str) -> None``.
                ``http_status`` is 0 when no HTTP response was received.
        """
        cb = self._make_http_status_cb(callback) if callback else sb_http_status_callback_t()
        _lib.sb_submit_hardware_probe_result(
            self._handle, _encode(run_id), _encode(result_json), cb, None
        )


def create_game_state(config):
    # type: (Config) -> GameState
    """Create a :class:`GameState` from a fully-configured :class:`Config`.

    The config **must** have at least:

    * ``set_provider(...)``
    * ``set_machine_id(...)``
    * ``set_game_code_version(...)``
    * ``set_encrypted_key(...)`` **or** ``set_signer(...)``

    Args:
        config: A :class:`Config` instance.

    Returns:
        A new :class:`GameState` instance.

    Raises:
        RuntimeError: If the C SDK returns a null handle.

    Example::

        config = scorbit.Config()
        config.set_provider("myprovider")
        config.set_machine_id(4419)
        config.set_game_code_version("1.0.0")
        config.set_encrypted_key(key)

        with scorbit.create_game_state(config) as gs:
            ...
    """
    handle = _lib.sb_create_game_state(config._handle)
    if not handle:
        raise RuntimeError(
            "Failed to create game state. Check that the Config has "
            "provider, machine_id, game_code_version, and authentication "
            "(encrypted_key or signer) set."
        )
    return GameState(handle, config)
