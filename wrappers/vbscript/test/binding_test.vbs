' Scorbit SDK
'
' (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
'
' MIT License
'
' The above copyright notice and this permission notice shall be included in
' all copies or substantial portions of the Software.

' Scorbit SDK for VBScript: tests against mocks of the plugin's Scorbit.Table.
' cscript //nologo binding_test.vbs <path to scorbit-<version>.vbs>
' Exits with the number of failed checks. An error escaping the binding stops cscript with a
' non-zero exit, which fails the test as well.

Dim failures, checks
failures = 0
checks = 0

Sub Check(cond, name)
    checks = checks + 1
    If Not cond Then
        failures = failures + 1
        WScript.Echo "FAIL " & name
    End If
End Sub

' The plugin's object at ApiLevel 2. Records every call, in order.
Class MockTable
    Public Log, Uuid, Auth, System, Session, Text, Result, CountValue
    Private claimed(9), ids(9), names(9), initials(9), links(9), pictures(9)

    Private Sub Class_Initialize()
        Log = "" : Uuid = "" : Auth = -1 : System = -1 : Session = -1 : Text = "" : Result = -1 : CountValue = 2
    End Sub

    Public Sub SetPlayer(slot, isClaimed, id, name, ini, link, pic)
        claimed(slot) = isClaimed : ids(slot) = id : names(slot) = name
        initials(slot) = ini : links(slot) = link : pictures(slot) = pic
    End Sub

    Private Sub Add(s)
        Log = Log & s & ";"
    End Sub

    Public Property Get ApiLevel() : ApiLevel = 2 : End Property
    Public Property Get Version() : Version = "0.1.0" : End Property
    Public Sub Declare(id) : Uuid = id : Add "Declare " & id : End Sub
    Public Sub SetGameStarted(origin) : Add "SetGameStarted " & origin : End Sub
    Public Sub SetGameFinished() : Add "SetGameFinished" : End Sub
    Public Sub SetCurrentBall(ball) : Add "SetCurrentBall " & ball : End Sub
    Public Sub SetActivePlayer(player) : Add "SetActivePlayer " & player : End Sub
    Public Sub SetScore(player, score, feature) : Add "SetScore " & player & "," & score & "," & feature : End Sub
    Public Sub AddMode(mode) : Add "AddMode " & mode : End Sub
    Public Sub AddModeExpiring(seconds, mode) : Add "AddModeExpiring " & seconds & "," & mode : End Sub
    Public Sub SetModeCompleted(mode) : Add "SetModeCompleted " & mode : End Sub
    Public Sub RemoveMode(mode) : Add "RemoveMode " & mode : End Sub
    Public Sub ClearModes() : Add "ClearModes" : End Sub
    Public Sub Commit() : Add "Commit" : End Sub

    Public Property Get AuthStatus() : AuthStatus = Auth : End Property
    Public Property Get SystemState() : SystemState = System : End Property
    Public Property Get SessionState() : SessionState = Session : End Property
    Public Property Get StatusText() : StatusText = Text : End Property
    Public Property Get MachineUuid() : MachineUuid = Uuid : End Property

    Public Function RequestTopScores(scope, period, vpinFilter, limit, since)
        Add "RequestTopScores " & scope & "," & period & "," & vpinFilter & "," & limit & "," & since
        RequestTopScores = 7
    End Function
    Public Function TopScoresResult(id) : TopScoresResult = Result : End Function
    Public Function TopScoresAge(id) : TopScoresAge = 12 : End Function
    Public Function TopScoresCount(id) : TopScoresCount = CountValue : End Function
    Public Function TopScoresRank(id, i) : TopScoresRank = i + 1 : End Function
    Public Function TopScoresScore(id, i) : TopScoresScore = 9007199254740992 - i : End Function
    Public Function TopScoresName(id, i) : TopScoresName = "Name" & i : End Function
    Public Function TopScoresInitials(id, i) : TopScoresInitials = "AB" & i : End Function
    Public Function TopScoresFlags(id, i) : TopScoresFlags = 3 : End Function
    Public Function TopScoresCreated(id, i) : TopScoresCreated = "2026-11-01T12:00:00Z" : End Function
    Public Sub ReleaseTopScores(id) : Add "ReleaseTopScores " & id : End Sub

    Public Function PlayerClaimed(slot) : PlayerClaimed = (claimed(slot) = True) : End Function
    Public Function PlayerId(slot) : PlayerId = ids(slot) & "" : End Function
    Public Function PlayerName(slot) : PlayerName = names(slot) & "" : End Function
    Public Function PlayerInitials(slot) : PlayerInitials = initials(slot) & "" : End Function
    Public Function PlayerClaimLink(slot) : PlayerClaimLink = links(slot) & "" : End Function
    Public Function PlayerPicture(slot) : PlayerPicture = pictures(slot) & "" : End Function
End Class

' A plugin from before ApiLevel: the game calls only.
Class MockLegacy
    Public Log
    Private Sub Class_Initialize() : Log = "" : End Sub
    Public Sub Declare(id) : Log = Log & "Declare " & id & ";" : End Sub
    Public Sub SetScore(player, score, feature) : Log = Log & "SetScore " & player & "," & score & "," & feature & ";" : End Sub
    Public Sub AddModeExpiring(seconds, mode) : Log = Log & "AddModeExpiring " & seconds & "," & mode & ";" : End Sub
    Public Sub Commit() : Log = Log & "Commit;" : End Sub
End Class

' Every member raises.
Class MockThrowing
    Public Property Get ApiLevel() : ApiLevel = 2 : End Property
    Public Sub Declare(id) : Err.Raise 5 : End Sub
    Public Sub SetScore(player, score, feature) : Err.Raise 5 : End Sub
    Public Sub Commit() : Err.Raise 5 : End Sub
    Public Property Get AuthStatus() : Err.Raise 5 : End Property
    Public Property Get MachineUuid() : Err.Raise 5 : End Property
    Public Function RequestTopScores(scope, period, vpinFilter, limit, since) : Err.Raise 5 : End Function
    Public Function TopScoresResult(id) : Err.Raise 5 : End Function
    Public Function PlayerName(slot) : Err.Raise 5 : End Function
End Class

If WScript.Arguments.Count <> 1 Then
    WScript.Echo "usage: cscript //nologo binding_test.vbs <scorbit-<version>.vbs>"
    WScript.Quit 2
End If
Dim fso, file
Set fso = CreateObject("Scripting.FileSystemObject")
Set file = fso.OpenTextFile(WScript.Arguments(0), 1)
ExecuteGlobal file.ReadAll
file.Close

Const UUID = "01234567-89ab-cdef-0123-456789abcdef"

' --- no plugin: Scorbit.Table cannot be created here
ScorbitInit UUID
Check Not ScorbitAvailable(), "no plugin: not available"
Check ScorbitApiLevel() = 0, "no plugin: level 0"
ScorbitSetGameStarted SCORBIT_GAME_STARTED_BY_BUTTON
ScorbitSetScore 1, 100, 0
ScorbitAddModeExpiring "MB:Multiball", 3
ScorbitCommit
Check ScorbitGetStatus() = SCORBIT_STATUS_UNKNOWN, "no plugin: status unknown"
Check ScorbitGetMachineUuid() = "", "no plugin: no uuid"
Check ScorbitRequestTopScores(0, 0, "", 0) = 0, "no plugin: no request id"
Check ScorbitTopScoresResult(1) = SCORBIT_TOP_SCORES_NONE, "no plugin: no result"
Check ScorbitPlayerName(1) = "", "no plugin: no player"
Check Not ScorbitPlayerClaimed(1), "no plugin: unclaimed"
Check ScorbitPollEvent() = SCORBIT_EVENT_NONE, "no plugin: no event"
ScorbitInitWith Nothing, UUID
Check Not ScorbitAvailable(), "Nothing: not available"

' --- ApiLevel 2
Dim m
Set m = New MockTable
ScorbitInitWith m, UUID
Check ScorbitAvailable(), "level 2: available"
Check ScorbitApiLevel() = 2, "level 2: level"
Check m.Uuid = UUID, "level 2: declared"
m.Log = ""
ScorbitSetGameStarted SCORBIT_GAME_STARTED_FROM_LOBBY
ScorbitSetCurrentBall 2
ScorbitSetActivePlayer 3
ScorbitSetScore 3, 12340, 5
ScorbitAddMode "MB:Multiball"
ScorbitAddModeExpiring "JP:Jackpot", 4
ScorbitSetModeCompleted "NA:Forty Thieves"
ScorbitRemoveMode "MB:Multiball"
ScorbitClearModes
ScorbitCommit
ScorbitSetGameFinished
Check m.Log = "SetGameStarted 1;SetCurrentBall 2;SetActivePlayer 3;SetScore 3,12340,5;AddMode MB:Multiball;" _
    & "AddModeExpiring 4,JP:Jackpot;SetModeCompleted NA:Forty Thieves;RemoveMode MB:Multiball;ClearModes;Commit;SetGameFinished;", _
    "level 2: game calls in order, AddModeExpiring's arguments swapped: " & m.Log

m.Auth = SCORBIT_STATUS_AUTHENTICATED_PAIRED : m.System = 0 : m.Session = 1 : m.Text = "ok"
Check ScorbitGetStatus() = SCORBIT_STATUS_AUTHENTICATED_PAIRED, "level 2: status"
Check ScorbitGetSystemState() = SCORBIT_SYSTEM_OK, "level 2: system state"
Check ScorbitGetSessionState() = SCORBIT_SESSION_TRACKING, "level 2: session state"
Check ScorbitGetStatusText() = "ok", "level 2: status text"
Check ScorbitGetMachineUuid() = UUID, "level 2: uuid"

m.Log = ""
Dim id
id = ScorbitRequestTopScores(SCORBIT_LEADERBOARD_SCOPE_GAME, SCORBIT_LEADERBOARD_PERIOD_30D, "2026-01-01T00:00:00Z", SCORBIT_LEADERBOARD_VPIN_ONLY)
Check id = 7, "level 2: request id"
Check m.Log = "RequestTopScores 2,2,1,20,2026-01-01T00:00:00Z;", "level 2: request mapped to the native order: " & m.Log
Check ScorbitTopScoresResult(id) = SCORBIT_TOP_SCORES_NONE, "level 2: pending"
m.Result = SCORBIT_TOP_SCORES_OK
Check ScorbitTopScoresResult(id) = SCORBIT_TOP_SCORES_OK, "level 2: result"
Check ScorbitTopScoresAge(id) = 12, "level 2: age"
Check ScorbitTopScoresCount(id) = 2, "level 2: count"
Check ScorbitTopScoresRank(id, 1) = 2, "level 2: rank"
Check ScorbitTopScoresScore(id, 0) = 9007199254740992, "level 2: score exact at 2^53"
Check ScorbitTopScoresName(id, 1) = "Name1", "level 2: name"
Check ScorbitTopScoresInitials(id, 0) = "AB0", "level 2: initials"
Check (ScorbitTopScoresFlags(id, 0) And SCORBIT_SCORE_FLAG_VERIFIED) <> 0, "level 2: flags"
Check ScorbitTopScoresCreated(id, 0) = "2026-11-01T12:00:00Z", "level 2: created"
m.CountValue = Null
Check ScorbitTopScoresCount(id) = 0, "level 2: a Null count reads 0"
m.CountValue = "many"
Check ScorbitTopScoresCount(id) = 0, "level 2: a text count reads 0"
m.CountValue = 1000
Check ScorbitTopScoresCount(id) = SCORBIT_TOP_SCORES_MAX, "level 2: a huge count is clamped"
m.CountValue = 2
m.Log = ""
ScorbitSetScore 1, 3000000000, 0
Check m.Log = "SetScore 1,3000000000,0;", "level 2: a score past 2^31 passes through: " & m.Log
m.Log = ""
ScorbitReleaseTopScores id
Check m.Log = "ReleaseTopScores 7;", "level 2: release"

' --- events, against the same object
Check ScorbitPollEvent() = SCORBIT_EVENT_PAIRING_STATUS_CHANGED, "events: status known"
Check ScorbitPollEvent() = SCORBIT_EVENT_NONE, "events: nothing else yet"
m.SetPlayer 1, False, "", "", "", "https://example.invalid/claim/1", ""
Check ScorbitPollEvent() = SCORBIT_EVENT_PLAYERS_UPDATED, "events: slot 1 open to claim"
Check ScorbitPlayerClaimLink(1) = "https://example.invalid/claim/1", "players: claim link"
Check ScorbitPollEvent() = SCORBIT_EVENT_NONE, "events: no repeat"
m.SetPlayer 2, True, "p2", "Jane", "JD", "", "C:\pics\p2.png"
Check ScorbitPollEvent() = SCORBIT_EVENT_PLAYERS_UPDATED, "events: slot 2 claimed"
Check ScorbitPollEvent() = SCORBIT_EVENT_PLAYER_PICTURE_READY, "events: picture ready"
Check ScorbitEventSlot() = 2, "events: picture slot"
Check ScorbitPlayerClaimed(2), "players: claimed"
Check ScorbitPlayerId(2) = "p2" And ScorbitPlayerName(2) = "Jane" And ScorbitPlayerInitials(2) = "JD", "players: claimed fields"
Check ScorbitPlayerPicture(2) = "C:\pics\p2.png", "players: picture"
m.Auth = SCORBIT_STATUS_AUTHENTICATED_UNPAIRED
Check ScorbitPollEvent() = SCORBIT_EVENT_PAIRING_STATUS_CHANGED, "events: status change"
Check ScorbitPollEvent() = SCORBIT_EVENT_NONE, "events: drained"

' --- ApiLevel 1: an object without ApiLevel
Dim legacy
Set legacy = New MockLegacy
ScorbitInitWith legacy, UUID
Check ScorbitApiLevel() = 1, "level 1: no ApiLevel member is level 1"
ScorbitSetScore 1, 5, 0
ScorbitAddModeExpiring "MB:Multiball", 0
ScorbitCommit
ScorbitSetCurrentBall 1
Check legacy.Log = "Declare " & UUID & ";SetScore 1,5,0;AddModeExpiring 0,MB:Multiball;Commit;", "level 1: game calls: " & legacy.Log
Check ScorbitGetStatus() = SCORBIT_STATUS_UNKNOWN, "level 1: no read-back"
Check ScorbitRequestTopScores(0, 0, "", 0) = 0, "level 1: no leaderboards"
Check ScorbitPollEvent() = SCORBIT_EVENT_NONE, "level 1: no events"

' --- an object whose members raise: nothing reaches the table
Dim thrower
Set thrower = New MockThrowing
ScorbitInitWith thrower, UUID
ScorbitSetScore 1, 5, 0
ScorbitCommit
ScorbitAddMode "MB:Multiball"
Check ScorbitGetStatus() = SCORBIT_STATUS_UNKNOWN, "raising: status unknown"
Check ScorbitGetMachineUuid() = "", "raising: uuid empty"
Check ScorbitRequestTopScores(0, 0, "", 0) = 0, "raising: no request id"
Check ScorbitTopScoresResult(1) = SCORBIT_TOP_SCORES_NONE, "raising: no result"
Check ScorbitPlayerName(1) = "", "raising: no name"
Check ScorbitPollEvent() = SCORBIT_EVENT_NONE, "raising: no event"

' --- Err after every routine: neither an escaping error nor a swallowed one left behind,
' against an object whose members raise and one missing most of them.
Dim calls
calls = Array("ScorbitSetGameStarted 0", "ScorbitSetGameFinished", "ScorbitSetCurrentBall 1", "ScorbitSetActivePlayer 1", _
    "ScorbitSetScore 1, 5, 0", "ScorbitAddMode ""M""", "ScorbitAddModeExpiring ""M"", 3", "ScorbitSetModeCompleted ""M""", _
    "ScorbitRemoveMode ""M""", "ScorbitClearModes", "ScorbitCommit", "x = ScorbitGetStatus()", "x = ScorbitGetMachineUuid()", _
    "x = ScorbitGetSystemState()", "x = ScorbitGetSessionState()", "x = ScorbitGetStatusText()", _
    "x = ScorbitRequestTopScores(0, 0, """", 0)", "x = ScorbitTopScoresResult(1)", "x = ScorbitTopScoresAge(1)", _
    "x = ScorbitTopScoresCount(1)", "x = ScorbitTopScoresRank(1, 0)", "x = ScorbitTopScoresScore(1, 0)", _
    "x = ScorbitTopScoresName(1, 0)", "x = ScorbitTopScoresInitials(1, 0)", "x = ScorbitTopScoresFlags(1, 0)", _
    "x = ScorbitTopScoresCreated(1, 0)", "ScorbitReleaseTopScores 1", "x = ScorbitPlayerClaimed(1)", "x = ScorbitPlayerId(1)", _
    "x = ScorbitPlayerName(1)", "x = ScorbitPlayerInitials(1)", "x = ScorbitPlayerClaimLink(1)", "x = ScorbitPlayerPicture(1)", _
    "x = ScorbitPollEvent()", "x = ScorbitEventSlot()", "x = ScorbitAvailable()", "x = ScorbitApiLevel()")

Sub ErrAfterEvery(obj, label)
    Dim c, x
    On Error Resume Next
    ScorbitInitWith obj, UUID
    Check Err.Number = 0, label & ": ScorbitInitWith left Err " & Err.Number
    For Each c In calls
        Err.Clear
        Execute c
        Check Err.Number = 0, label & ": " & c & " left Err " & Err.Number
    Next
End Sub
ErrAfterEvery New MockThrowing, "raising"
ErrAfterEvery New MockLegacy, "level 1"
ErrAfterEvery Nothing, "no plugin"

WScript.Echo checks & " checks, " & failures & " failed"
WScript.Quit failures
