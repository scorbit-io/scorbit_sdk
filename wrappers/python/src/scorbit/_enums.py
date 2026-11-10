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
    LimitExceeded = 6


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

    AlreadyRunning = 2
    """The game was already in progress when the host started; no competition attempt starts."""


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


class TableMappingsResult(IntEnum):
    """How the last read of the VPIN table mappings ended (append-only)."""

    NoneYet = 0
    """Not fetched yet; the list is empty."""

    Ok = 1
    """The list is the server's current one."""

    NotVpinDevice = 2
    """The device is not a VPIN device; the list is empty."""

    NoOwner = 3
    """The device has no owner; the list is empty."""

    Unavailable = 4
    """Network, server or reply error; the last good list is kept."""


class PublishResult(IntEnum):
    """How a :meth:`GameState.publish` call ended (append-only).

    Every value but ``Ok`` is a plain failure; the SDK never retries.
    """

    Ok = 0
    Invalid = 1
    TooLarge = 2
    NotConnected = 3
    Refused = 4


class SwitchMachineResult(IntEnum):
    """How a :meth:`GameState.switch_machine` call ended (append-only)."""

    Ok = 0
    AlreadyAttached = 1
    GameActive = 2
    Busy = 3
    NotAuthenticated = 4
    NotVpinDevice = 5
    NoOwner = 6
    TargetNotFound = 7
    TargetNotOwned = 8
    TargetNotVpin = 9
    Network = 10
    Failed = 11


class TableMatchResult(IntEnum):
    """How a :meth:`GameState.match_table_mapping` call ended (append-only)."""

    Mapped = 0
    """The mapping and its target are filled."""

    NeedsMapping = 1
    """409 venuemachine_needed: map the table first."""

    UnknownRom = 2
    UnknownSignature = 3
    SignatureTooLarge = 4

    Invalid = 5
    """Any 400, or refused before sending: both kinds of evidence, or bad signatures."""

    NotVpinDevice = 6
    NoOwner = 7

    NotAuthenticated = 8
    """Authentication failed for good, or its wait ended."""

    Network = 9
    """No response, timeout, 5xx or 429."""

    Failed = 10
    """Anything else: a malformed 200, an unknown 4xx, or an api without the route."""


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
    TableMappingsUpdated = 10
    MachineChanged = 11
    AchievementUnlocked = 12
    CompetitionGoalReceived = 13

    # Internal / scorbitd events
    _None = 1000
    ScorbitdUpdateReceived = 1001
    ScorbitdUpdated = 1002
    FirmwaresListReceived = 1003
    HardwareProbeRequested = 1004
    ProbeFlashRequested = 1005
    LiveDmdRequested = 1006


class LogLevel(IntEnum):
    """Log severity levels."""

    Debug = 0
    Info = 1
    Warn = 2
    Error = 3


class AchievementArtwork(IntEnum):
    """The artwork handed over with an ``AchievementUnlocked`` event."""

    None_ = 0
    """No artwork (the default): nothing is downloaded."""

    Frame = 1
    """The DMD frame, for the machine's display (a PNG today)."""

    Image = 2
    """The icon, a high-resolution image for LCD machines."""


class AchievementPresentation(IntEnum):
    """When ``AchievementUnlocked`` is raised for an achievement the machine decided.

    Whatever the mode, it is raised once per unlock. An achievement only Scorbit can
    decide (a trophy, or one that depends on another title's achievements) is always
    raised once Scorbit grants it.
    """

    Granted = 0
    """Once Scorbit grants it (the default): never for something Scorbit then refuses,
    one round trip later, and not while offline."""

    Local = 1
    """The moment the machine decides it, even offline, with its artwork only if that is
    already in memory; not again on the grant. Not authoritative: a refusal that follows is
    not signalled, and the Scorbit app won't show it."""
