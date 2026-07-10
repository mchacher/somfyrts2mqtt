# Architecture 022 — RTS device types (foundation + Gate)

## Touched modules

| File                         | Change                                                                                   |
| ---------------------------- | ---------------------------------------------------------------------------------------- |
| `include/device_profile.h`   | **New.** Pure header: `DeviceType` + `uses_position`/`name`/`from_u8` + `SOMFY_TOGGLE = 0x0C`. |
| `include/nvs_store.h` / `.cpp`| `Remote.device_type` + `set_device_type` + `r.<hex>.typ` key; SCHEMA stays 1.            |
| `include/mqtt.h`             | `Command::Toggle` (+ `command_to_str`); additive `Type` in `publish_shutter_state`.      |
| `src/mqtt.cpp`               | `Toggle` cmnd verb; emit `"Type"` in SENSOR + stat **only when type != Shutter**.        |
| `src/orchestrator.cpp`       | Button resolution (Toggle → `0x0C`); Gate = no state; `tick`/`set_position`/durations guard non-positional. |
| `src/web_ui.cpp`             | Type selector; Gate command cell = Open/Stop/Close + Toggle button, blind state; `/api/remotes/<id>/type` setter + `.../toggle` command. |
| `test/test_device_profile/`  | **New.** Native tests for the profile mapping, `from_u8`, `valid_toggle_button`.         |
| docs                         | `docs/mqtt-api.md`, `docs/web-ui.md`, `README.md`, `CLAUDE.md`.                          |

## Decisions

**A Gate uses the dedicated Somfy RTS `Toggle` command `0x0C`.** A single-button
gate motor cycles open/stop/close/stop on this one command — it is its own RTS
button code, distinct from Up/Down/My (which do those actions separately). Source:
rstrouse/ESPSomfy-RTS (`somfy_commands::Toggle = 0xC`; `isToggle()` for the
`*gate1`/`garage1` single-button shade types). The Gate exposes **Open / Close /
Stop *and* Toggle** so both a distinct motor and a single-button one are covered.
*Rejected drafts:* a binary Open/Close/Stop cover (fabricates a state the bridge
cannot know), and a configurable Up/My/Down toggle button (those are not a toggle
— they do open/stop/close; the real toggle is 0x0C). It tracks **no position**
(a gate is blind). `rf::send_somfy` already takes a raw button byte and the lib's
`buildFrame` shifts any 4-bit value into place, so `0x0C` emits with no extra work.

**One orthogonal axis, default = Shutter, read-with-default.** A missing
`r.<hex>.typ` reads 0 = Shutter → today's exact behaviour, **zero migration**.
**`SCHEMA_VERSION` deliberately stays 1** (as iter 014): `init()` treats any
stored `schema != SCHEMA_VERSION` as "refusing to operate", so a bump would
brick access to every remote on an upgraded box. Additive keys need no bump, and
the motor never needs re-pairing (remote_id + rolling_code are untouched).

**`device_profile` stays a pure header** (no NVS/MQTT/Arduino dep), like
`shutter_state.h` / `ota_guard.h`. `nvs_store` stores type/button as raw bytes;
the orchestrator/mqtt/web layers interpret via `device_profile`.

**MQTT stays a strict superset; `Type` is emitted only for non-Shutter** →
a pure-shutter deployment is byte-identical on the wire. The bridge emits the
hint + the `Toggle` verb; mapping to a Sowel `gate` is the plugin's job (later).

## Flow (handle_command)

```
handle_command(id, cmd):
  remote = get_remote(id)                        # carries device_type + toggle_button
  if cmd == Toggle: button = SOMFY_TOGGLE (0x0C)                          # no invert
  else:             button = command_to_button(apply_invert(cmd))
  if button == 0: drop
  persist rolling_code (before emit)
  rf::send_somfy(id, next_code, button, repeat)   # Toggle repeat = 1
  if uses_position(profile):                       # Shutter -> iter-014 machine, verbatim
      run time-based state machine
  # else (Gate): blind -> no position/direction/state update
  publish_stat(remote, rt)                         # stat/SENSOR gain "Type" iff non-Shutter
  publish_sensor_aggregated()
```

RF emission, rolling-code persist-before-emit, and `invert` (for non-toggle
commands) are shared and unchanged across every type. `tick()` skips
non-positional remotes.
