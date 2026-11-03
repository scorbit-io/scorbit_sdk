#!/usr/bin/env python3
# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
"""Parity of the VBScript binding with the SDK's game-facing API.

- Every public GameState method and every event is in the binding, present but commented out,
  or listed as out of scope below, with the reason. Methods in the binding take the SDK's
  parameters, callbacks aside, since VBScript has no optional arguments.
- Every routine that touches the plugin's object is guarded before it does.
- The binding's constants carry the SDK's values.

usage: parity.py <include/scorbit_sdk dir> <scorbit .vbs or .vbs.in>
"""
import re
import sys

# Present but commented out: served only to public, coin-operated cabinets.
COMMENTED_OUT = {"setCreditsDropped", "setCreditsStatus"}
COMMENTED_OUT_EVENTS = {"GameStartRequested", "CreditsAddRequested", "CreditsStatusRequested", "PricingReceived"}

# Not a table's to call.
OUT_OF_SCOPE = {
    "getMachineSerial": "the Scorbit device's serial, shared by every table; a table is identified by its uuid",
    "getPairDeeplink": "pairing is managed outside the table",
    "requestPairCode": "pairing is managed outside the table",
    "requestUnpair": "pairing is managed outside the table",
    "setCapabilities": "device plumbing",
    "uploadDiagnostics": "device plumbing",
    "reportDeviceState": "device plumbing",
    "requestPairMachine": "internal",
    "setProbeFirmware": "device plumbing",
    "setPbspkcommFirmwareId": "device plumbing",
    "submitHardwareProbeResult": "device plumbing",
    "submitProbeFlashStage": "device plumbing",
    "submitProbeFlashResult": "device plumbing",
    "download": "device plumbing",
    "downloadWithOptions": "device plumbing",
    "downloadBuffer": "device plumbing",
    "tableMappings": "VPIN host plumbing: the host daemon, not the table, maps and switches machines",
    "refreshTableMappings": "VPIN host plumbing",
    "venueMachine": "VPIN host plumbing",
    "switchMachine": "VPIN host plumbing",
}
# The "no event" value, which ScorbitPollEvent returns when there is nothing to report.
SENTINEL_EVENTS = {"None"}
OUT_OF_SCOPE_EVENTS = {
    "ConfigReceived": "device plumbing",
    "DiagnosticsUploadRequested": "device plumbing",
    "DiagnosticsUploaded": "device plumbing",
    "ScorbitdUpdateReceived": "device plumbing",
    "ScorbitdUpdated": "device plumbing",
    "FirmwaresListReceived": "device plumbing",
    "HardwareProbeRequested": "device plumbing",
    "ProbeFlashRequested": "device plumbing",
    "TableMappingsUpdated": "VPIN host plumbing",
    "MachineChanged": "VPIN host plumbing",
}

# Methods the parser must find, so a header change it cannot follow fails instead of passing.
CORE = {"setGameStarted", "setScore", "addModeExpiring", "commit", "getStatus", "requestTopScores"}

# Binding constants that mirror C enums: binding prefix -> (C header, C prefix).
ENUMS = {
    "SCORBIT_EVENT_": ("event_types_c.h", "SB_EVT_"),
    "SCORBIT_STATUS_": ("net_types_c.h", "SB_NET_"),
    "SCORBIT_GAME_STARTED_": ("net_types_c.h", "SB_GAME_STARTED_"),
    "SCORBIT_LEADERBOARD_SCOPE_": ("net_types_c.h", "SB_LEADERBOARD_SCOPE_"),
    "SCORBIT_LEADERBOARD_PERIOD_": ("net_types_c.h", "SB_LEADERBOARD_PERIOD_"),
    "SCORBIT_LEADERBOARD_VPIN_": ("net_types_c.h", "SB_LEADERBOARD_VPIN_"),
}
# The binding's own value for "not known yet"; the SDK has no such enumerator.
BINDING_ONLY = {"SCORBIT_STATUS_UNKNOWN"}


def split_top(text):
    """Split on commas outside (), <>, {} and []."""
    parts, depth, cur = [], 0, ""
    for ch in text:
        if ch in "(<{[":
            depth += 1
        elif ch in ")>}]":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    return [p.strip() for p in parts if p.strip()]


def sdk_methods(game_state_h):
    """name -> parameter counts (one per overload), callbacks excluded; and lines it could not read."""
    text = open(game_state_h, encoding="utf-8").read()
    text = text[text.index("public:"):text.index("END OF PUBLIC INTERFACE")]
    lines = text.split("\n")
    methods, unread = {}, []
    i = 0
    while i < len(lines):
        line = lines[i]
        # A member declaration sits at the class's indent; bodies, comments and labels do not count.
        if not re.match(r"^    [^\s/*}]", line) or line.strip().endswith(":") or line.strip().startswith(("return", "{")):
            i += 1
            continue
        m = re.search(r"\b([A-Za-z_]\w*)\s*\(", line)
        if not m:
            i += 1
            continue
        name = m.group(1)
        # The parameter list, across lines, to its matching parenthesis.
        rest, depth, params, j = line[m.end():], 1, "", i
        while depth > 0:
            for ch in rest:
                if ch == "(":
                    depth += 1
                elif ch == ")":
                    depth -= 1
                    if depth == 0:
                        break
                params += ch
            if depth > 0:
                j += 1
                if j >= len(lines):
                    break
                rest = "\n" + lines[j]
        i = j + 1
        if name in ("GameState", "operator") or name[0].isupper():
            continue  # constructors, operators
        if not name[0].islower():
            unread.append(line.strip())
            continue
        counted = [p for p in split_top(params) if "Callback" not in p.split("=")[0]]
        methods.setdefault(name, set()).add(len(counted))
    return methods, unread


def sdk_events(event_types_h):
    text = open(event_types_h, encoding="utf-8").read()
    body = text[text.index("enum class EventType"):]
    body = body[body.index("{") + 1:body.index("};")]
    return re.findall(r"^\s*([A-Z][A-Za-z0-9]*)\s*=", body, re.M)


def c_enum_values(header):
    """Every enumerator in the header's C enums, numbered as C numbers them."""
    values = {}
    text = re.sub(r"/\*.*?\*/", "", open(header, encoding="utf-8").read(), flags=re.S)
    for body in re.findall(r"typedef\s+enum\s*\w*\s*\{(.*?)\}", text, re.S):
        nxt = 0
        for item in body.split(","):
            item = re.sub(r"//.*", "", item).strip()
            m = re.match(r"([A-Z_][A-Z0-9_]*)\s*(?:=\s*(-?\d+))?$", item)
            if not m:
                continue
            nxt = int(m.group(2)) if m.group(2) else nxt
            values[m.group(1)] = nxt
            nxt += 1
    return values


def binding(vbs):
    """Routines (name -> parameter count, body), commented-out routines, and constants."""
    active, commented, consts, commented_consts, unread = {}, {}, {}, {}, []
    current = None
    for line in open(vbs, encoding="utf-8"):
        line = line.rstrip("\r\n")
        if re.match(r"^\s*'?\s*(?:(?:Public|Private)\s+)?(?:Sub|Function)\s+Scorbit", line) and not re.match(
                r"^(\s*'\s*)?(?:(?:Public|Private)\s+)?(?:Sub|Function)\s+Scorbit[A-Z][A-Za-z0-9_]*\s*\([^)]*\)\s*$", line):
            unread.append(line.strip())
        if re.match(r"^\s*'?\s*Const\s+SCORBIT_", line) and not re.match(
                r"^(\s*'\s*)?Const\s+SCORBIT_[A-Z0-9_]+\s*=\s*(-?\d+|\"[^\"]*\")\s*$", line):
            unread.append(line.strip())
        m = re.match(r"^(\s*'\s*)?(?:(?:Public|Private)\s+)?(?:Sub|Function)\s+Scorbit([A-Z][A-Za-z0-9]*)\s*\(([^)]*)\)", line)
        if m:
            count = len([p for p in m.group(3).split(",") if p.strip()])
            if m.group(1):
                commented[m.group(2)] = count
            else:
                active[m.group(2)] = (count, [])
                current = m.group(2)
            continue
        if current and re.match(r"^End (Sub|Function)", line):
            current = None
            continue
        if current:
            active[current][1].append(line)
        m = re.match(r"^(\s*'\s*)?Const\s+(SCORBIT_[A-Z0-9_]+)\s*=\s*(-?\d+)\s*$", line)
        if m:
            (commented_consts if m.group(1) else consts)[m.group(2)] = int(m.group(3))
    return active, commented, consts, commented_consts, unread


def upper_snake(camel):
    return re.sub(r"(?<!^)(?=[A-Z])", "_", camel).upper()


def main():
    include, vbs = sys.argv[1], sys.argv[2]
    methods, unread = sdk_methods(include + "/game_state.h")
    events = sdk_events(include + "/event_types.h")
    active, commented, consts, commented_consts, unread_vbs = binding(vbs)
    errors = [f"cannot read a binding line: {line}" for line in unread_vbs]

    for line in unread:
        errors.append(f"cannot read a GameState declaration: {line}")
    for name in sorted(CORE - set(methods)):
        errors.append(f"the parser found no GameState::{name}; game_state.h changed in a way it cannot follow")

    for name, counts in sorted(methods.items()):
        exported = name[0].upper() + name[1:]
        if name in OUT_OF_SCOPE:
            if exported in active:
                errors.append(f"{name} is listed out of scope but the binding exports Scorbit{exported}")
            continue
        if name in COMMENTED_OUT:
            if exported not in commented:
                errors.append(f"{name}: Scorbit{exported} is not commented out in the binding")
            elif max(counts) != commented[exported]:
                errors.append(f"{name}: Scorbit{exported} takes {commented[exported]} arguments, the SDK {max(counts)}")
            continue
        if exported not in active:
            errors.append(f"{name}: Scorbit{exported} is not exported in the binding")
        elif max(counts) != active[exported][0]:
            errors.append(f"{name}: Scorbit{exported} takes {active[exported][0]} arguments, the SDK {max(counts)}")
    for name in sorted((set(OUT_OF_SCOPE) | COMMENTED_OUT) - set(methods)):
        errors.append(f"{name} is listed but GameState has no such method")

    for name in events:
        const = "SCORBIT_EVENT_" + upper_snake(name)
        if name in SENTINEL_EVENTS:
            if const not in consts:
                errors.append(f"event {name}: {const} is not defined")
        elif name in OUT_OF_SCOPE_EVENTS:
            if const in consts:
                errors.append(f"event {name} is listed out of scope but {const} is defined")
        elif name in COMMENTED_OUT_EVENTS:
            if const not in commented_consts:
                errors.append(f"event {name}: {const} is not present commented out")
        elif const not in consts:
            errors.append(f"event {name}: {const} is not defined")
    for name in sorted((set(OUT_OF_SCOPE_EVENTS) | COMMENTED_OUT_EVENTS | SENTINEL_EVENTS) - set(events)):
        errors.append(f"event {name} is listed but EventType has no such event")

    # Guarded: On Error Resume Next before the first use of the plugin's object.
    for name, (_, body) in sorted(active.items()):
        guard = next((i for i, l in enumerate(body) if l.strip() == "On Error Resume Next"), None)
        use = next((i for i, l in enumerate(body) if "ScorbitImpl_Table." in l), None)
        if use is not None and (guard is None or guard > use):
            errors.append(f"Scorbit{name} uses the plugin's object before On Error Resume Next")

    # Values, against the C enums.
    checked = 0
    for prefix, (header, c_prefix) in ENUMS.items():
        c_values = c_enum_values(f"{include}/{header}")
        for const, value in {**consts, **commented_consts}.items():
            if not const.startswith(prefix) or const in BINDING_ONLY:
                continue
            c_name = c_prefix + const[len(prefix):]
            if c_name not in c_values:
                errors.append(f"{const} has no C counterpart {c_name}")
            elif c_values[c_name] != value:
                errors.append(f"{const} is {value}, {c_name} is {c_values[c_name]}")
            checked += 1
        if prefix == "SCORBIT_EVENT_":
            continue
        for c_name in sorted(c_values):
            if c_name.startswith(c_prefix) and prefix + c_name[len(c_prefix):] not in {**consts, **commented_consts}:
                errors.append(f"{c_name} has no binding constant {prefix + c_name[len(c_prefix):]}")

    covered = len([n for n in methods if n not in OUT_OF_SCOPE and n not in COMMENTED_OUT])
    print(f"{len(methods)} GameState methods: {covered} in the binding, {len(COMMENTED_OUT)} commented out, "
          f"{len(OUT_OF_SCOPE)} out of scope; {len(events)} events; {len(active)} routines checked for guards; "
          f"{checked} constant values checked")
    for e in errors:
        print("FAIL " + e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
