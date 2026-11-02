# Scorbit SDK for VBScript tables

The SDK's game API for [Visual Pinball X](https://github.com/vpinball/vpinball) table scripts. It
calls the `Scorbit.Table` object that the Scorbit plugin for VPX provides, so a table reports its
game to Scorbit the way a machine using this SDK does: the same calls, under the same names with a
`Scorbit` prefix.

## Using it

The binding is one file, named for the SDK version it comes from: `scorbit-<version>.vbs`. Load the
version the table was written for, once, from the table script's top level (here `x.y.z` stands for
that version):

```vbscript
On Error Resume Next
ExecuteGlobal GetTextFile("scorbit-x.y.z.vbs")
On Error Goto 0
ScorbitInit "<machine uuid>"
```

**Ship that file beside the table.** VPX finds it there. Without the file none of the `Scorbit` names
exist, so the table's first call to one stops it. With the file, the table runs on a VPX without the
Scorbit plugin too: every routine is then a silent no-op, and every function returns its "none"
value. No routine raises an error into the table or leaves one in `Err`.

VBScript has no optional arguments, so **every argument is required**. Where the SDK has a default,
pass it: `ScorbitSetScore player, score, 0`, `ScorbitAddModeExpiring mode, 3`.

```vbscript
ScorbitSetGameStarted SCORBIT_GAME_STARTED_BY_BUTTON
ScorbitSetCurrentBall 1
ScorbitSetActivePlayer 1
ScorbitSetScore 1, 12340, 0
ScorbitAddModeExpiring "MB:Multiball", 3
ScorbitCommit
' ...
ScorbitSetModeCompleted "NA:The Tale of the Forty Thieves"
ScorbitCommit
' ...
ScorbitSetGameFinished
```

Scores are exact up to 2^53.

## Routines

| SDK (`GameState`) | Binding | Needs `ApiLevel` |
|---|---|---|
| `setGameStarted`, `setGameFinished`, `setCurrentBall`, `setActivePlayer` | `ScorbitSetGameStarted origin`, `ScorbitSetGameFinished`, `ScorbitSetCurrentBall ball`, `ScorbitSetActivePlayer player` | 1 |
| `setScore` | `ScorbitSetScore player, score, feature` | 1 |
| `addMode`, `addModeExpiring`, `removeMode`, `clearModes` | `ScorbitAddMode mode`, `ScorbitAddModeExpiring mode, seconds`, `ScorbitRemoveMode mode`, `ScorbitClearModes` | 1 |
| `setModeCompleted` | `ScorbitSetModeCompleted mode` (matched against achievement conditions) | 1 |
| `commit` | `ScorbitCommit` | 1 |
| `getStatus` | `ScorbitGetStatus()`: a `SCORBIT_STATUS_*` value, `SCORBIT_STATUS_UNKNOWN` until known | 2 |
| `getMachineUuid` | `ScorbitGetMachineUuid()`: the uuid the table declared | 2 |
| `requestTopScores(scope, period, since, vpinFilter, callback)` | `ScorbitRequestTopScores(scope, period, since, vpinFilter)`: a request id, polled (below) | 2 |
| `PlayersUpdated`, `player_info.h` | `ScorbitPlayerClaimed(slot)`, `ScorbitPlayerId`, `ScorbitPlayerName`, `ScorbitPlayerInitials`, `ScorbitPlayerClaimLink`: slots 1 to 9 | 2 |
| `PlayerPictureReady` | `ScorbitPlayerPicture(slot)`: the path of the claimed player's picture, a JPEG, or `""` | 2 |

A picture file belongs to the plugin. It lasts until the slot's claim or picture changes, the table
ends or the plugin unloads, so load it or copy it rather than keeping the path.

Beyond the SDK, the game's status as Scorbit indicates it, which needs level 2:
`ScorbitGetSystemState()` and `ScorbitGetSessionState()`, which read `SCORBIT_SYSTEM_UNKNOWN` and
`SCORBIT_SESSION_UNKNOWN` before the first status, and `ScorbitGetStatusText()`.

`ScorbitAvailable()` says whether the plugin is present, and `ScorbitApiLevel()` gives its
`ApiLevel`: 0 without the plugin, and 1 for a plugin from before `ApiLevel`. A routine that needs a
higher level than the plugin has is a no-op.

## Events and answers

VBScript has no callbacks the SDK could call, so answers and events are polled from a timer.

```vbscript
Sub ScorbitTimer_Timer()
    Dim evt
    evt = ScorbitPollEvent()
    Do While evt <> SCORBIT_EVENT_NONE
        Select Case evt
            Case SCORBIT_EVENT_PAIRING_STATUS_CHANGED: ' ScorbitGetStatus()
            Case SCORBIT_EVENT_PLAYERS_UPDATED: ' ScorbitPlayerName(slot), ScorbitPlayerClaimLink(slot), ...
            Case SCORBIT_EVENT_PLAYER_PICTURE_READY: ' ScorbitPlayerPicture(ScorbitEventSlot())
        End Select
        evt = ScorbitPollEvent()
    Loop
End Sub
```

A leaderboard request returns an id, and its answer is kept under that id until released:

```vbscript
id = ScorbitRequestTopScores(SCORBIT_LEADERBOARD_SCOPE_MACHINE, SCORBIT_LEADERBOARD_PERIOD_ALL_TIME, "", SCORBIT_LEADERBOARD_VPIN_ANY)
' later, from a timer
If ScorbitTopScoresResult(id) = SCORBIT_TOP_SCORES_OK Then
    For i = 0 To ScorbitTopScoresCount(id) - 1
        ' ScorbitTopScoresRank(id, i), ScorbitTopScoresName(id, i), ScorbitTopScoresScore(id, i), ...
    Next
    ScorbitReleaseTopScores id
End If
```

`ScorbitTopScoresResult` is `SCORBIT_TOP_SCORES_OK` or a reason the request failed once the answer
arrives. Up to `SCORBIT_TOP_SCORES_MAX` (20) entries come back. Before then it reads
`SCORBIT_TOP_SCORES_NONE`, but so does an id the plugin no longer holds:
- an id of 0, which means no answer can come;
- a released id;
- an id from before the table restarted;
- an id evicted by newer requests, since the plugin keeps the last `SCORBIT_TOP_SCORES_HELD` (8).

So check for an id of 0, and give up on a request after a time of the table's own choosing.

## Not in the binding

- **Credits, pricing and starting a game from the app** (`setCreditsDropped`, `setCreditsStatus`,
  `GameStartRequested`, `CreditsAddRequested`, `CreditsStatusRequested`, `PricingReceived`) apply to
  public, coin-operated cabinets. They are present in the file, commented out, so their names are
  reserved.
- **Pairing** (`getPairDeeplink`, `requestPairCode`, `requestUnpair`) is managed outside the table.
- **The device's serial** (`getMachineSerial`) belongs to the Scorbit device, not the table, which is
  identified by its uuid.
- **Device plumbing** (diagnostics, device state, firmware, downloads) is not a game's to call.

## Building and testing

```sh
cmake -S wrappers/vbscript -B build/vbscript
ctest --test-dir build/vbscript --output-on-failure
```

This writes `build/vbscript/scorbit-<version>.vbs` at configure time, so run `cmake` again after
editing `scorbit.vbs.in`. The suite in `tests/` runs the same tests. The tests are:
- **Parity**, anywhere Python 3 runs:
  - every public `GameState` method and every event is in the binding with the SDK's parameters
    (callbacks aside), present but commented out, or listed out of scope with a reason;
  - every routine is guarded before it touches the plugin's object;
  - the constants carry the SDK's values.
- **The binding**, on Windows under `cscript`, against mocks of `Scorbit.Table`. It covers the
  no-plugin path, an object from before `ApiLevel`, the argument order and polled events, and checks
  that a raising object never reaches the table.
