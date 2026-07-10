# 022 — RTS device types (foundation + Gate)

## Goal
Let a remote declare what kind of RTS equipment it drives, so the bridge can
behave correctly beyond roller shutters — starting with a **sliding gate
("portail coulissant")** — **without changing anything for existing remotes or
the current MQTT integration**. Introduce one new orthogonal axis
(`device_type`, default `Shutter`) plus the first non-shutter type.

## Background
Today every remote is modelled as a Tasmota **Shutter** (time-based position
0-100 + durations + `invert`). A Somfy RTS gate motor in **sequential mode**
cycles open → stop → close → stop on a **single repeated RTS button** — the
motor does the sequencing, the remote just re-sends one button. It has no
position and no state feedback (RTS is blind). So a gate is not a cover with
distinct Open/Close/Stop; it is a **single-button toggle**. We want a
type-aware bridge that stays a "dumb" transponder and is strictly additive: a
legacy all-shutter deployment must be byte-for-byte identical in RF, NVS and MQTT.

## Scope

In scope (firmware only):
- **`include/device_profile.h`** — new pure header: `enum class DeviceType :
  uint8_t { Shutter = 0, Gate = 1 }`, `uses_position()`, `name()`, `from_u8()`
  (unknown → `Shutter`, defensive), plus `SOMFY_TOGGLE = 0x0C` (the dedicated
  Somfy RTS Toggle command). Native-testable.
- **NVS** (`nvs_store`) — per-remote `r.<hex>.typ` (device type, default 0 =
  Shutter) key + `Remote.device_type` + `set_device_type()`. A missing key reads
  0 → **existing remotes read as Shutter automatically**. **`SCHEMA_VERSION`
  stays 1**: an additive key needs no bump, and bumping would trip `init()`'s
  "unknown schema → refusing to operate" on a box upgraded from v0.4.0, wiping
  access to every stored remote.
- **Command** — new `mqtt::Command::Toggle`: emit the Somfy RTS Toggle command
  `0x0C` (no `invert`; it is its own button code).
- **Orchestrator** — `handle_command` resolves the button (`Toggle` → `0x0C`;
  else the mapped/inverted command). `Shutter` keeps today's time-based state
  machine **verbatim**; a `Gate` tracks **no position/direction/state** and is
  skipped by the 1 Hz `tick()`. `set_position`/duration setters warn-and-ignore
  for a Gate.
- **MQTT** — new `cmnd/<root>/<name>/Toggle` verb (Open/Close/Stop already
  exist). Additive `"Type":"gate"` in the SENSOR object and `stat` JSON, emitted
  **only for non-Shutter** remotes.
- **Web UI (admin portal)** — per-remote **Type** selector; a Gate command cell
  shows **Open / Stop / Close** plus a **Toggle** (🔄) button, and a blind "—"
  state (no position); the duration/position/invert calibration hides for a Gate.
- **Tests** — native `test_device_profile` (mapping, `from_u8`, `SOMFY_TOGGLE`).
- **Docs** — device type, the `Toggle` verb + `Type` hint contract, gate notes.

Out of scope:
- **No Sowel changes.** The bridge only emits the `Type` hint + `Toggle` verb;
  presenting a gate as a Sowel `gate` (device_class, state) is a separate
  Sowel-plugin task. A gate stays operable via the current plugin meanwhile.
- A *distinct* Open/Close/Stop gate (non-sequential motor) — that fits the
  Shutter type; not modelled as a separate device here.
- Other types (`Pulse`/`OnOff`) — later increments on the same foundation.
- No `invert` change (orthogonal modifier for positional types).

## Acceptance criteria
- [ ] Existing all-shutter deployment: RF frames, NVS keys and SENSOR/stat JSON
      are unchanged (no `Type` field when every remote is Shutter).
- [ ] A remote set to `Gate`: the UI command cell shows Open / Stop / Close + a
      **Toggle** button; `Toggle` emits the Somfy Toggle command `0x0C`; SENSOR
      reports `"Type":"gate"`; no position, no auto-stop tick; calibration hidden.
- [ ] `cmnd/<root>/<name>/Toggle` emits button `0x0C` (verified on the RF log /
      by the gate cycling); Open/Close/Stop still emit Up/Down/My.
- [ ] `device_type` defaults to Shutter for every pre-existing NVS remote (no
      re-pairing).
- [ ] `SCHEMA_VERSION` stays 1; a box OTA-upgraded from v0.4.0 keeps all its
      remotes (init() never hits "refusing to operate"). **No re-pairing of the
      motor** (remote_id + rolling_code untouched).
- [ ] `pio run` clean on both envs, `pio check` clean, `pio test -e native`
      green incl. `test_device_profile`.
- [ ] CI green on the PR.
