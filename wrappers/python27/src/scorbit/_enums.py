# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Python enum mirrors of the C SDK enum types."""

from __future__ import absolute_import

from enum import IntEnum


class Error(IntEnum):
    """Error codes returned by SDK operations."""

    Success = 0
    Unknown = 1
    AuthFailed = 2
    NotPaired = 3
    ApiError = 4
    FileError = 5


class AuthStatus(IntEnum):
    """Authentication and pairing status."""

    NotAuthenticated = 0
    Authenticating = 1
    AuthenticatedCheckingPairing = 2
    AuthenticatedUnpaired = 3
    AuthenticatedPaired = 4
    AuthenticationFailed = 5


class GameStartOrigin(IntEnum):
    """How the game was started."""

    StartButton = 0
    """Game started by the machine when player presses the Start button."""

    FromLobby = 1
    """Game started explicitly via Scorbit app request."""


class LeaderboardScope(IntEnum):
    """Leaderboard source to query."""

    Machine = 0
    """Specific paired venue machine leaderboard."""

    Variant = 1
    """Shared variant leaderboard for the paired title."""

    Game = 2
    """Shared game leaderboard across variants."""


class LeaderboardPeriod(IntEnum):
    """Time bucket to query for leaderboard results."""

    AllTime = 0
    """All-time leaderboard."""

    Days14 = 1
    """Rolling 14-day leaderboard."""

    Days30 = 2
    """Rolling 30-day leaderboard."""

    Days90 = 3
    """Rolling 90-day leaderboard."""

    Days180 = 4
    """Rolling 180-day leaderboard."""

    Days365 = 5
    """Rolling 365-day leaderboard."""


class LeaderboardVpinFilter(IntEnum):
    """How virtual pinball scores should be filtered."""

    Any = 0
    """Include both virtual and physical scores."""

    VpinOnly = 1
    """Include only virtual pinball scores."""

    RealOnly = 2
    """Exclude virtual pinball scores."""


class Capability(object):
    """Device capability flags (combine with bitwise OR).

    Unlike the other enums this is a plain class with integer constants
    because ``enum34`` does not include ``IntFlag``.
    """

    StartGame = 1 << 0
    """Game can be started remotely."""

    CreditDrop = 1 << 1
    """Machine can accept coin drop events."""


class EventType(IntEnum):
    """Types of events delivered via the event callback."""

    GameStartRequested = 0
    CreditsAddRequested = 1
    CreditsStatusRequested = 2
    ConfigReceived = 3
    PlayersUpdated = 4
    PlayerPictureReady = 5
    DiagnosticsUploadRequested = 6
    DiagnosticsUploaded = 7
    PricingReceived = 8
    PairingStatusChanged = 9

    # Internal / scorbitd events
    _None = 1000
    ScorbitdUpdateReceived = 1001
    ScorbitdUpdated = 1002
    FirmwaresListReceived = 1003


class LogLevel(IntEnum):
    """Log severity levels."""

    Debug = 0
    Info = 1
    Warn = 2
    Error = 3
