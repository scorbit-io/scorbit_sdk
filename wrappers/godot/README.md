# Scorbit SDK for Godot 4

A Godot add-on that lets a game report to Scorbit from **GDScript**. It is a GDExtension over the
SDK's C API, plus a `Scorbit` autoload with the API game code calls.

Built against the Godot 4.3 API, it loads in Godot 4.3 and every later 4.x release. Platforms:
Windows x86_64, macOS, and Linux x86_64 and arm64.

## Build

```bash
cmake -S wrappers/godot -B build/godot -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/godot
```

The add-on is staged in `build/godot/addons/scorbit`, with its native libraries in `bin/`. Godot
loads the `template_debug` library in the editor and in debug exports, and `template_release` in
release exports, so ship both: build again with `-DSCORBIT_GODOT_TARGET=template_debug` into
another build directory and copy its `bin/` files alongside.

In a `template_debug` build, if a Godot 4 binary is on `PATH` (or given with
`-DGODOT_EXECUTABLE=...`), `ctest` runs a headless session with the native library and without it.

## Install in a game

1. Copy `addons/scorbit` into the project's `addons/` folder.
2. Enable **Scorbit** under *Project > Project Settings > Plugins*. That registers the `Scorbit`
   autoload.
3. Start a game state and report the game:

```gdscript
func _ready() -> void:
	Scorbit.players_updated.connect(_on_players_updated)
	Scorbit.start({
		"provider": "your_provider",
		"machine_id": 1234,
		"game_code_version": "1.0.0",
		"encrypted_key": "...",  # from encrypt_tool; required
	})

func _on_ball_started(ball: int) -> void:
	Scorbit.set_current_ball(ball)
	Scorbit.commit()
```

`provider`, `machine_id`, `game_code_version` and `encrypted_key` are required, and `start()`
fails without any of them. `encrypted_key` is required because the add-on doesn't yet sign with a
machine's hardware key (TPM or tap pad).

Call `commit()` after each batch of changes. SDK events arrive as signals on the main thread:
`sdk_event`, `pairing_status_changed`, `game_start_requested`, `players_updated` and
`player_picture_ready`.

If the native library can't load, `Scorbit.is_available()` is `false` and every call does
nothing, so the game still runs.

## What the add-on covers

Everything a game uses from the SDK. `scorbit.gd` documents each call:

- **Game state:** `set_game_started`, `set_game_finished`, `set_current_ball`, `set_active_player`,
  `set_score`, modes (`add_mode`, `add_mode_expiring`, `remove_mode`, `clear_modes`,
  `set_mode_completed`), and `commit`.
- **Auto-start and credits:** advertise them with
  `set_capabilities(CAPABILITY_START_GAME | CAPABILITY_CREDIT_DROP)`. Then:
  - `game_start_requested` asks the game to start; start it with
    `set_game_started(STARTED_FROM_LOBBY)`.
  - `credits_add_requested(credits, transaction)` asks to add credits; confirm with
    `set_credits_dropped`.
  - `credits_status_requested` is answered with `set_credits_status`.
  - `pricing_received` carries the machine's pricing.
- **Players:** `players_updated` and `player_picture_ready`.
- **Leaderboards:** `request_top_scores(scope, period, since, vpin_filter)` answers with
  `top_scores_received`.
- **Pairing:** `get_pair_deeplink`, `request_pair_code`, `request_pair_machine`, `request_unpair`,
  `pairing_status_changed`, `get_status`, `get_machine_uuid` and `get_machine_serial`.
- **Downloads:** `download` and `download_buffer`.
- **Diagnostics and logging:** `diagnostics_upload_requested` with `upload_diagnostics`,
  `report_device_state`, and SDK log lines as `log_message` after `enable_log_messages`.

Requests answer through signals, with an error code that is 0 on success. All signals are emitted
on the main thread.

## Notes

- The SDK library ships beside the extension (`bin/`), and exports copy it next to the game, or
  into `Contents/Frameworks` on macOS. In a released add-on it updates itself in place, as on
  other hosts, so the game's install folder must be writable by the user that runs the game.
- An add-on built from source with the commands above does **not** update itself: it isn't a
  production build and has no ABI tag, so the SDK's updater refuses it. Use it for development.
- The device key is stored at `user://scorbit/device_key` unless `key_path` says otherwise. Keep
  it between runs: a new key means a new device to pair.

See `examples/godot_example` for a complete project.
