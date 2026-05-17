# Scorbit SDK - Python wrapper

Pure-Python wrapper for the [Scorbit SDK](https://scorbit.io) C library. Works on **Python 3.6+** with no compilation: it uses `ctypes` against the stable C ABI.

> For Python 2.7, use [`wrappers/python27`](../python27/).

## Native SDK required

`pip install scorbit` installs **only** the Python wrapper (PyPI wheel). You must also install the **native** shared library (`libscorbit_sdk.so` / `.dylib` / `.dll`) for the **same version** from [GitHub releases](https://github.com/scorbit-io/scorbit_sdk/releases) (for example `scorbit_sdk-1.99.66-arm64_u18.deb` or `.tar.gz`). If the library is missing, `import scorbit` prints a version-matched release URL and suggested filenames for your platform.

## Prerequisites

Install the Scorbit SDK shared library on the system. The wrapper loads it in this order:

1. Directory from **`SCORBIT_SDK_PATH`**
2. **`/opt/scorbit/lib`** when using official Linux packages
3. Normal dynamic linker paths (`LD_LIBRARY_PATH`, `DYLD_LIBRARY_PATH`, `/usr/local/lib`, etc.)

| Platform | Library file           |
|----------|------------------------|
| Linux    | `libscorbit_sdk.so`    |
| macOS    | `libscorbit_sdk.dylib` |
| Windows  | `scorbit_sdk.dll`      |

## Installation

```bash
pip install scorbit==1.99.66
```

Then install the native SDK from the matching release, for example [1.99.66](https://github.com/scorbit-io/scorbit_sdk/releases/tag/1.99.66), and use the `.deb` or `.tar.gz` for your architecture and ABI tag (see the root [README](../../README.md)).

From this repository:

```bash
cd wrappers/python
pip install .
```

### Building a wheel from the SDK repo

`make python` runs `scripts/python-build.sh`. By default the wheel is built in **Docker** using the same `dilshodm/gcc-builder:<tag>` image as Linux SDK builds (`DOCKER_RELEASE` in the repo root), via `docker_build_wheel` in `scripts/_common.sh`. Use **`SCORBIT_PYTHON_NO_DOCKER=1`**, run already inside a container (`/.dockerenv`), or skip Docker when it is unavailable to build with the current host Python instead.

### Setting the library path

```bash
# Linux / macOS
export SCORBIT_SDK_PATH=/opt/scorbit/lib

# Windows (PowerShell)
$env:SCORBIT_SDK_PATH = "C:\scorbit\lib"
```

## Quick start

Set **`set_event_callback`** (and optional key callbacks) on **`Config` before** **`create_game_state`**. Use a **`with`** block or call **`gs.destroy()`** when done (see [`examples/python_example/main.py`](../../examples/python_example/main.py)).

```python
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
        print(f"Game start requested with {players} player(s)")

    elif event.type == scorbit.EventType.PlayersUpdated:
        players = event.get_players_updated()
        for num, info in players.items():
            print(f"  Player {num}: {info.preferred_name}")

config.set_event_callback(on_event)

config.set_save_key_callback(lambda key: open("device_key.json", "w").write(key))
config.set_load_key_callback(
    lambda: open("device_key.json").read()
    if os.path.exists("device_key.json") else ""
)

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
| `Capability`      | `StartGame`, `CreditDrop` (combine with `|`) |
| `EventType`       | `GameStartRequested`, `CreditsAddRequested`, `CreditsStatusRequested`, `ConfigReceived`, `PlayersUpdated`, `PlayerPictureReady` |
| `LogLevel`        | `Debug`, `Info`, `Warn`, `Error` |

### `Config`

Setters return `self` for chaining.

| Method | Description |
|--------|-------------|
| `set_provider(name)` | *Required.* Provider name. |
| `set_machine_id(id)` | *Required.* Scorbit machine ID. |
| `set_game_code_version(ver)` | *Required.* Game code version string. |
| `set_encrypted_key(key)` | Auth key (non-TPM). |
| `set_signer(callback)` | TPM signer: `(digest: bytes) -> bytes`. |
| `set_hostname(host)` | `"production"`, `"staging"`, or URL. |
| `set_uuid(uuid)` | Device UUID (from MAC if omitted). |
| `set_serial_number(sn)` | Device serial number. |
| `set_auto_download_player_pics(bool)` | Download profile pictures. |
| `set_score_features(list, version)` | Score feature names and version. |
| `set_event_callback(cb)` | `(event: Event) -> None`. |
| `set_save_key_callback(cb)` | `(key: str) -> None`. |
| `set_load_key_callback(cb)` | `() -> str`. |

### `GameState`

From `scorbit.create_game_state(config)`. Supports `with` / `__exit__` cleanup.

| Method / property | Description |
|-------------------|-------------|
| `set_game_started(origin)` | Start a session. |
| `set_game_finished()` | End session (auto-commits). |
| `set_current_ball(ball)` | Ball 1-9. |
| `set_active_player(player)` | Active player 1-9. |
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

`type` is `EventType`. Helpers return parsed data or `None` if the type does not match.

| Method | Returns |
|--------|---------|
| `get_game_start_requested()` | `int` (player count) or `None` |
| `get_credits_add_requested()` | `(int, str)` or `None` |
| `get_pricing_received()` | `PricingInfo` or `None` |
| `get_config_received()` | JSON `str` or `None` |
| `get_players_updated()` | `dict[int, PlayerInfo]` or `None` |
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

If logger callback is registered it will receive log lines from SDK.

```python
def my_logger(message, level, file, line, timestamp):
    print(f"[{level.name}] {message}")

scorbit.add_logger_callback(my_logger, max_length=512)
scorbit.reset_logger()  # when done
```

## Threading

- C calls release the GIL (`ctypes.CDLL`).
- C callbacks (events, async results, logger) acquire the GIL via `CFUNCTYPE`; you do not manage the GIL yourself.
- Callbacks run on SDK threads; guard shared mutable state with `threading.Lock`.
- An `atexit` handler stops callback trampolines during shutdown to avoid crashes.

## License

MIT - see the LICENSE file in the repository root.
