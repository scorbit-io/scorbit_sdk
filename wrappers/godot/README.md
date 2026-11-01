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
		"encrypted_key": "...",  # from encrypt_tool; omit where the machine has a hardware key
	})

func _on_ball_started(ball: int) -> void:
	Scorbit.set_current_ball(ball)
	Scorbit.commit()
```

Call `commit()` after each batch of changes. SDK events arrive as signals on the main thread:
`sdk_event`, `pairing_status_changed`, `game_start_requested`, `players_updated` and
`player_picture_ready`.

If the native library can't load, `Scorbit.is_available()` is `false` and every call does
nothing, so the game still runs.

## Notes

- The SDK library ships beside the extension (`bin/`), and exports copy it next to the game, or
  into `Contents/Frameworks` on macOS. It updates itself in place, as on other hosts, so the
  game's install folder must be writable by the user that runs the game.
- The device key is stored at `user://scorbit/device_key` unless `key_path` says otherwise. Keep
  it between runs: a new key means a new device to pair.

See `examples/godot_example` for a complete project.
