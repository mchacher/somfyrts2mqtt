# Architecture 022 — RTS device types (foundation + Gate)

## Touched modules

| File                         | Change                                                                                   |
| ---------------------------- | ---------------------------------------------------------------------------------------- |
| `include/device_profile.h`   | **New.** Pure header: `DeviceType` enum + `uses_position()` / `name()` / `from_u8()`.    |
| `include/nvs_store.h`        | `Remote.device_type` (uint8); `set_device_type()`; `SCHEMA_VERSION` stays 1 (additive).  |
| `src/nvs_store.cpp`          | `remote_type_key()`; seed in `add_remote`; read in `get_remote`/`list_remotes`; remove on delete. |
| `src/orchestrator.cpp`       | Profile branch in `handle_command`; guard `set_position`/durations; skip non-positional in `tick`. |
| `src/mqtt.cpp`               | Emit `"Type"` in SENSOR + stat JSON **only when type != Shutter**; map gate Position 0/100. |
| `src/web_ui.cpp`             | Per-remote Type `<select>`; `/api/remotes/<id>/type/<n>` setter; hide calibration for non-positional. **Extra UI care here (admin portal).** |
| `test/test_device_profile/`  | **New.** Native tests for the profile mapping + `from_u8` fallback.                       |
| docs                         | `docs/web-ui.md`, `docs/mqtt-api.md`, `README.md`, `CLAUDE.md`: device types + `Type` hint. |

## Decisions

**One orthogonal axis, default = Shutter, read-with-default.** Arduino
`Preferences` return the type-default on a missing key, so a `remote_type_<hex>`
absent on legacy remotes reads 0 = Shutter → today's exact behaviour with **zero
migration**. **`SCHEMA_VERSION` deliberately stays 1** (same call iter 014 made):
`init()` treats any stored `schema != SCHEMA_VERSION` as "unknown → refusing to
operate", so bumping it would brick access to every remote on a box upgraded
from v0.4.0. An additive per-remote key needs no bump. *Rejected: a
`Cover`/`Gate`/`Awning` type that folds in `invert`* — that would reinterpret
existing awning-via-invert remotes. `invert` stays an orthogonal modifier.

**`device_profile` is a pure header, no NVS/MQTT/Arduino dependency**, mirroring
`shutter_state.h` / `ota_guard.h`. `nvs_store` stores the type as a raw `uint8`
(no enum dependency); the orchestrator/mqtt/web layers interpret it via
`device_profile`. `from_u8()` maps any unknown value back to `Shutter` so a
forward-written NVS (future type) degrades safely on older firmware.

**MQTT stays a strict superset; `Type` is emitted only for non-Shutter.** This
guarantees a pure-shutter deployment is byte-identical on the wire — the primary
non-regression requirement. Consumers detect a gate by the presence of
`"Type":"gate"`; its absence means shutter. The bridge stays "dumb": it emits
the hint, it does not implement HA device classes (that is the Sowel plugin's
job, later — not this repo).

**Gate behaviour = binary cover.** `Open`→Up (state 100), `Close`→Down (state
0), `Stop`→My (no state change); no time-based estimate, `tick()` skips it. As a
convenience for HA covers that send position, `Position 100`→Open and `0`→Close;
other position values are ignored with a warn. Duration setters are inert. A
sliding gate and a garage door share this exact profile — only the label / future
device_class differ, so `Garage` is a trivial later addition.

## Flow (handle_command, per profile)

```
handle_command(id, cmd):
  remote = get_remote(id)                       # now carries device_type
  profile = device_profile::from_u8(remote.device_type)
  ... rolling code persist + RF emit (UNCHANGED for all types) ...
  if uses_position(profile):                    # Shutter -> today's code path, verbatim
      run time-based state machine (estimate/target/tick)
  else:                                          # Gate -> binary
      Up   -> position=100, direction=idle, target=100
      Down -> position=0,   direction=idle, target=0
      Stop -> no motion change
      persist position on Up/Down
  publish_stat(remote, rt)                       # stat/SENSOR gain "Type" iff non-Shutter
  publish_sensor_aggregated()
```

RF emission, rolling-code persist-before-emit, and `invert` handling are shared
and unchanged across every type.
