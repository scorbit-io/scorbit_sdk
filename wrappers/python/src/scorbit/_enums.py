# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Python enum mirrors of the C SDK enum types."""

from enum import IntEnum, IntFlag


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


class Capability(IntFlag):
    """Device capability flags (combine with bitwise OR)."""

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
    AchievementUpdated = 10

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


class AchievementStatus(IntEnum):
    """What happened to an achievement (``AchievementUpdated`` event)."""

    Progress = 0
    """Progress changed; reported at ball boundaries."""

    UnlockedLocally = 1
    """The machine decided the player earned it; it may be presented now."""

    Confirmed = 2
    """The server granted it."""

    AlreadyHeld = 3
    """The server says the player already held it: do not celebrate again."""

    Retracted = 4
    """The server refused an unlock the machine decided: withdraw it."""


class AchievementRuleType(IntEnum):
    """The twelve achievement rule types."""

    Mode = 0
    ModeCompleted = 1
    ModeStack = 2
    Score = 3
    Event = 4
    Session = 5
    TimerSession = 6
    TimerBall = 7
    TimerMode = 8
    TimerBetween = 9
    Ball = 10
    Achievement = 11


class AchievementComparison(IntEnum):
    """Rule predicates; all inclusive."""

    Eq = 0
    Le = 1
    Ge = 2
    Ne = 3


class AchievementEvaluation(IntEnum):
    """The window an achievement's rules are measured over."""

    InSession = 0
    Unlimited = 1


class AchievementScope(IntEnum):
    """What an achievement attaches to."""

    Game = 0
    Venue = 1
    Event = 2
    Global = 3
