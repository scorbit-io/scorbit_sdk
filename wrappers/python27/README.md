# Scorbit SDK — Python 2.7 wrapper

Pure-Python wrapper for the [Scorbit SDK](https://scorbit.io) C library for **Python 2.7**. Uses `ctypes` against the stable C ABI; no compilation.

> For Python 3.6+, use [`wrappers/python`](../python/).

## Prerequisites

| Requirement | Details |
|-------------|---------|
| Python | 2.7.x |
| **enum34** | Pulled in by `pip` (`install_requires`) |
| SDK shared library | Install separately (below) |

Load order for the shared library:

1. **`SCORBIT_SDK_PATH`**
2. **`/opt/scorbit/lib`** when using official Linux packages
3. Normal paths (`LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH`, `/usr/local/lib`, etc.)

| Platform | Library file           |
|----------|------------------------|
| Linux    | `libscorbit_sdk.so`    |
| macOS    | `libscorbit_sdk.dylib` |
| Windows  | `scorbit_sdk.dll`      |

## Installation

```bash
pip install scorbit
```

From this repository:

```bash
cd wrappers/python27
pip install .
```

### Building a wheel from the SDK repo

`make python27` runs `scripts/python27-build.sh`. By default it uses the **same Docker image** as the Linux SDK and Python 3 wheel (`dilshodm/gcc-builder:<tag>` from `DOCKER_RELEASE`), via `docker_build_wheel` in `scripts/_common.sh`. Set **`SCORBIT_PYTHON_NO_DOCKER=1`** to build on the host (or current container).

### Setting the library path

```bash
# Linux / macOS
export SCORBIT_SDK_PATH=/opt/scorbit/lib

# Windows (PowerShell)
$env:SCORBIT_SDK_PATH = "C:\scorbit\lib"
```

## Quick start

Set **`set_event_callback`** (and optional key callbacks) on **`Config` before** **`create_game_state`**. Use a **`with`** block or call **`gs.destroy()`** when done (see [`examples/python27_example/main.py`](../../examples/python27_example/main.py)).

```python
from __future__ import absolute_import, print_function

import os
import scorbit

config = scorbit.Config()
config.set_provider("myprovider")
config.set_machine_id(4419)
config.set_game_code_version("1.0.0")
config.set_encrypted_key("<your-encrypted-key>")

def on_event(event):
    if event.type == scorbit.EventType.GameStartRequested:
        players = event.get_game_start_requested()
        print("Game start requested with %d player(s)" % players)

    elif event.type == scorbit.EventType.PlayersUpdated:
        players = event.get_players_updated()
        for num, info in players.items():
            print("  Player %d: %s" % (num, info.preferred_name))

config.set_event_callback(on_event)

def _save_key(key):
    open("device_key.json", "w").write(key)

def _load_key():
    return open("device_key.json").read() if os.path.exists("device_key.json") else ""

config.set_save_key_callback(_save_key)
config.set_load_key_callback(_load_key)

with scorbit.create_game_state(config) as gs:
    gs.set_capabilities(scorbit.Capability.StartGame | scorbit.Capability.CreditDrop)

    gs.set_game_started(scorbit.GameStartOrigin.StartButton)
    gs.set_score(1, 42000, feature=2)
    gs.set_current_ball(1)
    gs.add_mode("MB:Multiball")
    gs.commit()

    gs.set_game_finished()
```

## API reference

### Enums

| Enum              | Values |
|-------------------|--------|
| `Error`           | `Success`, `Unknown`, `AuthFailed`, `NotPaired`, `ApiError`, `FileError` |
| `AuthStatus`      | `NotAuthenticated`, `Authenticating`, `AuthenticatedCheckingPairing`, `AuthenticatedUnpaired`, `AuthenticatedPaired`, `AuthenticationFailed` |
| `GameStartOrigin` | `StartButton`, `FromLobby` |
| `Capability`      | `StartGame`, `CreditDrop` — integer constants; combine with `|` (`enum34` has no `IntFlag`) |
| `EventType`       | `GameStartRequested`, `CreditsAddRequested`, `CreditsStatusRequested`, `ConfigReceived`, `PlayersUpdated`, `PlayerPictureReady` |
| `LogLevel`        | `Debug`, `Info`, `Warn`, `Error` |

### `Config`

Setters return `self` for chaining.

| Method | Description |
|--------|-------------|
| `set_provider(name)` | *Required.* Provider name. |
| `set_machine_id(id)` | *Required.* Machine ID. |
| `set_game_code_version(ver)` | *Required.* Game code version. |
| `set_encrypted_key(key)` | Auth key (non-TPM). |
| `set_signer(callback)` | TPM signer: callable taking digest `bytes`, returns `bytes`. |
| `set_hostname(host)` | `"production"`, `"staging"`, or URL. |
| `set_uuid(uuid)` | Device UUID (from MAC if omitted). |
| `set_serial_number(sn)` | Serial number. |
| `set_auto_download_player_pics(bool)` | Download profile pictures. |
| `set_score_features(list, version)` | Score feature names and version. |
| `set_event_callback(cb)` | Event handler. |
| `set_save_key_callback(cb)` | Persist key string. |
| `set_load_key_callback(cb)` | Load key string. |

### `GameState`

From `scorbit.create_game_state(config)`. Supports `with` / `__exit__`.

| Method / property | Description |
|-------------------|-------------|
| `set_game_started(origin)` | Start a session. |
| `set_game_finished()` | End session (auto-commits). |
| `set_current_ball(ball)` | Ball 1–9. |
| `set_active_player(player)` | Active player 1–9. |
| `set_score(player, score, feature=0)` | Set score. |
| `add_mode(mode)` | Add mode string. |
| `add_mode_expiring(mode, secs=3)` | Expiring mode. |
| `remove_mode(mode)` | Remove mode. |
| `clear_modes()` | Clear all modes. |
| `commit()` | Push updates to the cloud. |
| `status` | `AuthStatus`. |
| `machine_uuid` | Machine UUID string. |
| `machine_serial` | Machine serial number (`int`, unsigned 64-bit; same as TPM / device info). |
| `pair_deeplink` | Pairing URL. |
| `request_top_scores(scope, period, since, vpin_filter, cb)` | Async leaderboard. |
| `request_pair_code(cb)` | Async pairing code. |
| `request_unpair(cb)` | Async unpair. |
| `set_capabilities(flags)` | Capability flags. |
| `set_credits_dropped(n, tx, ok)` | Credit drop report. |
| `set_credits_status(free, n, max, pricing)` | Credit status. |
| `download(url, file, ct, cb)` | Async file download. |
| `download_buffer(url, size, ct, cb)` | Async buffer download. |
| `destroy()` | Free resources (also from `__exit__`). |

### `Event`

Helpers return parsed data or `None` if the type does not match.

| Method | Returns |
|--------|---------|
| `type` | `EventType` |
| `get_game_start_requested()` | `int` or `None` |
| `get_credits_add_requested()` | `(int, str)` or `None` |
| `get_pricing_received()` | `PricingInfo` or `None` |
| `get_config_received()` | JSON `str` or `None` |
| `get_players_updated()` | `dict` (player number → `PlayerInfo`) or `None` |
| `get_player_picture_ready()` | `(int, bytes)` or `None` |

### `PlayerInfo`

| Attribute | Description |
|-----------|-------------|
| `id` | Scorbit player ID |
| `preferred_name` | Display name |
| `name` | Full name |
| `initials` | Initials |
| `picture_url` | Picture URL |
| `claim_deeplink` | Claim URL (empty slots) |
| `has_info()` | Whether the slot is claimed |

### Logger

If logger callback is registered it will receive log lines from SDK. The `level` argument is a `LogLevel` member (compare to `scorbit.LogLevel.Info`, etc.).

```python
def my_logger(message, level, file, line, timestamp):
    print("[%s] %s" % (level.name, message))

scorbit.add_logger_callback(my_logger, max_length=512)
scorbit.reset_logger()
```

## Threading

Same behavior as the Python 3 wrapper: C calls release the GIL; callbacks acquire it; use locks for shared state from callbacks; `atexit` silences trampolines during shutdown.

## Compared to the Python 3 wrapper

| Python 3 | Python 2.7 |
|----------|------------|
| `enum.IntFlag` for `Capability` | Plain class with integer constants |
| `requires-python >= 3.6` | `python_requires` 2.7 (not 3.x) |
| No extra deps | `enum34` |
| Wheel `py3-none-any` | Wheel `py2-none-any` |

Both publish as **`scorbit`**; `pip` picks the wheel for the running interpreter.

## License

MIT — see the LICENSE file in the repository root.
