# 022 — RTS device types (foundation + Gate)

## Goal
Let a remote declare what kind of RTS equipment it drives, so the bridge can
behave correctly beyond roller shutters — starting with a **sliding gate
("portail coulissant")** — **without changing anything for existing remotes or
the current MQTT integration**. Introduce one new orthogonal axis
(`device_type`, default `Shutter`) plus the first non-shutter type.

## Background
Today every remote is modelled as a Tasmota **Shutter** (time-based position
0-100 + durations + `invert`). A sliding gate works at the Up/Down/Stop level
but the position/duration model is meaningless for it — it is a binary
open/closed cover. We want a type-aware bridge that stays a "dumb" transponder
and is strictly additive: a legacy all-shutter deployment must be byte-for-byte
identical in RF, NVS and MQTT.

## Scope

In scope (firmware only):
- **`include/device_profile.h`** — new pure header: `enum class DeviceType :
  uint8_t { Shutter = 0, Gate = 1 }`, `uses_position()`, `name()`, `from_u8()`
  (unknown value → `Shutter`, defensive). Native-testable.
- **NVS** (`nvs_store`) — add a per-remote `remote_type_<hex>` key (uint8,
  default 0 = Shutter) + `Remote.device_type`. `add_remote` seeds it to 0;
  `get_remote`/`list_remotes` read it with default 0 → **existing remotes read
  as Shutter automatically**. **`SCHEMA_VERSION` stays 1** (additive change, same
  rationale as iter 014): bumping it would trip `init()`'s "unknown schema →
  refusing to operate" branch on any box upgraded from v0.4.0, wiping access to
  every stored remote — the exact regression we must avoid.
- **Orchestrator** — branch on the profile in `handle_command`: `Shutter`
  keeps today's time-based state machine unchanged; `Gate` (and any
  non-positional type) sets a **binary** state (Up → open/100, Down →
  closed/0, Stop → no motion change) and is skipped by the 1 Hz `tick()`.
  `set_position`/`set_open_duration`/`set_close_duration` warn-and-ignore for
  non-positional types (convenience: `Position 100`→Open, `0`→Close).
- **MQTT** — **additive only**: emit `"Type":"gate"` in the per-remote SENSOR
  object and `stat/<root>/<name>` JSON **only for non-Shutter remotes**. A
  pure-shutter setup produces exactly today's JSON (byte-identical).
- **Web UI** — a per-remote **Type** selector (default Shutter) via a new
  `/api/remotes/<id>/type/<n>` setter; hide duration/position calibration rows
  when the selected type is non-positional. Clean, tidy setup-row layout.
- **Tests** — native `test_device_profile` (mapping + `from_u8` fallback). The
  profile *decision* (positional vs binary) is fully unit-covered there; the
  `handle_command` branch itself stays HW-validated (it needs NVS/MQTT/RF, like
  the existing shutter path — see `test_orchestrator`'s own note).
- **Docs** — new device type, the additive `Type` hint contract, gate notes.

Out of scope:
- **No Sowel changes.** The bridge only *emits* the `Type` hint; consuming it
  (device_class gate, open/closed presentation) is a separate future
  Sowel-plugin task. Not touched here. A gate stays operable via the current
  plugin as a cover meanwhile — nothing breaks.
- Pedestrian / partial-open mode (a gate often has a dedicated channel for
  that) — possible later nicety, not in this increment.
- Other types (`Garage` — same binary profile, different label; `Pulse`
  single-button; `OnOff` lighting) — later increments on the same foundation.
- Home Assistant MQTT auto-discovery.
- Any change to the `invert` flag (stays an orthogonal modifier for positional
  types; awnings remain "Shutter + invert").

## Acceptance criteria
- [ ] Existing all-shutter deployment: RF frames, NVS keys and the SENSOR/stat
      JSON are unchanged (no `Type` field emitted when every remote is Shutter).
- [ ] A remote set to `Gate`: `Open`/`Close`/`Stop` emit Up/Down/My; SENSOR
      reports `"Type":"gate"` with Position 0/100 only; no auto-stop tick;
      duration/position calibration is inert and hidden in the UI.
- [ ] `device_type` defaults to Shutter for every pre-existing NVS remote (no
      re-pairing, no reconfiguration).
- [ ] `SCHEMA_VERSION` stays 1; a box OTA-upgraded from v0.4.0 keeps all its
      remotes and their behaviour (init() never hits "refusing to operate").
- [ ] `pio run` clean on both envs, `pio check` clean, `pio test -e native`
      green incl. `test_device_profile`.
- [ ] CI green on the PR.
