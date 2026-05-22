# 014 — shutter position + Tasmota-style topics

## Goal
Track and report each remote's shutter position (0-100 %) by calibrating open / close times, and switch the MQTT layer to Tasmota's Shutter semantics (Position, Direction, Target, OpenDuration, CloseDuration) keyed by remote name. This makes the bridge directly consumable by any Tasmota-aware client (Home Assistant, OpenHAB, custom MQTT subscribers) without a bespoke integration plugin.

## Background
RTS is unidirectional ; the motor never reports its state. The only way to expose position is time-based estimation. The user calibrates `OpenDuration` / `CloseDuration` once, then position is computed from `motion_started_ms` + elapsed time + direction. Drift is reset at each full Open (snap 100) / Close (snap 0). External operation (physical remote, wall switch) drifts silently until the next full Open / Close.

We also drop the legacy `somfy2mqtt/<HEXID>/{set,state,rolling_code}` topics : no integration was developed on top of them yet, so green field. The new layer matches Tasmota's Shutter MQTT subset, keyed by the remote's user-given name instead of an integer index.

## Scope

**In scope:**
- Per-remote NVS fields : `open_time_ms`, `close_time_ms` (uint32, 0 = uncalibrated), `position` (uint8, 0-100, snapshot persisted on motion stop).
- Per-remote `name` constrained to `[a-zA-Z0-9_-]{1,32}`. Uniqueness enforced. Existing remotes with non-conforming names (e.g. with spaces) stay in NVS but cannot be reached via MQTT until renamed via the UI ; we keep schema=1 (the new fields default-read as 0 / empty so no migration is needed).
- Configurable MQTT root topic (`mqtt.topic`), default `somfyrts2mqtt`, editable from the web UI. Multi-bridge setups must give each instance a distinct topic (the client_id is derived from the root). Constraint : `[a-zA-Z0-9_/-]{1,64}`, no leading / trailing slash, no `+` / `#`.
- New MQTT layer (matches Tasmota Shutter semantics, see `architecture.md`) :
  - **Subscribes** to `cmnd/<topic>/<name>/{Open,Close,Stop,Position,OpenDuration,CloseDuration,SetPosition}`.
  - **Publishes** `tele/<topic>/LWT` (retained + LWT), `tele/<topic>/SENSOR` (aggregated JSON, 1 Hz during motion + on every state change), `stat/<topic>/<name>` (JSON per remote after every cmd).
- Position state machine in `orchestrator` : per-remote (idle, opening, closing) with `motion_started_ms`, `start_position`, `target_position`. 1 Hz tick from `loop()` updates estimates and publishes telemetry ; when a non-extreme target is reached, the orchestrator enqueues a Stop emission ; full Open / Close lets the motor self-stop and snaps position to 100 / 0.
- Web UI : Open / Close duration inputs per row, a Position cell with current value + a `Set Position` input + the existing direction icons. Name input gets the regex enforced.
- Removal of the legacy `somfy2mqtt/<HEXID>/...` topics. The HTTP / web UI command endpoints stay unchanged.

**Out of scope:**
- Home Assistant Discovery (`homeassistant/cover/...`). Add in iter 015 if needed.
- ShutterInvert / Tilt / ShutterLock / Toggle. Subset of Tasmota's Shutter API ; we ship the most useful 7 commands listed above.
- Position slider in the web UI (numeric input only ; a follow-up iter can add UX polish).
- Auto-calibration wizard (Open then Close + chronometer). Manual duration input is enough for v1.
- Multi-bridge discovery / coexistence beyond the configurable `mqtt.topic`.

## Acceptance criteria
- [ ] Build clean on `esp32-c3-mini` and `esp32-wroom`.
- [ ] `pio check` zero defects, `pio test -e native` all green + new cases for position estimator + name validator + topic prefix validator.
- [ ] HW : configuring `OpenDuration=18` / `CloseDuration=20` then issuing `Position 50` opens or closes the shutter to ~50 % (within ±10 % of measured travel).
- [ ] HW : full Open (target=100) lets the motor self-stop, position snaps to 100. Same for Close to 0.
- [ ] HW : `SetPosition 30` updates the stored position without RF emission.
- [ ] HW : `cmnd/<topic>/<name>/Open` from `mosquitto_pub` moves the shutter ; `tele/<topic>/SENSOR` updates at 1 Hz during motion.
- [ ] HW : LWT topic shows `Online` after connect, `Offline` after a forced disconnect.
- [ ] HW : web UI editing `mqtt.topic` triggers a reconnect with the new topic prefix.
- [ ] CI green on the PR.

## Decisions
- **Tasmota semantics, but keyed by remote name instead of Shutter<idx>.** Names are already human-readable in the UI ; forcing the user to remember "Shutter3 = the kitchen one" adds friction with zero benefit. Topic structure : `cmnd/<topic>/<name>/<verb>`.
- **Position convention : 0 = closed, 100 = open** (matches Tasmota's default ; we do not expose ShutterInvert in v1).
- **`name` regex `[a-zA-Z0-9_-]{1,32}`, case-sensitive.** MQTT topics are case-sensitive ; we trust the user to be coherent. Enforced at add and edit ; uniqueness checked in NVS.
- **No NVS migration.** Schema stays at 1. The new Remote / MqttConfig fields are backward-readable (absent keys yield 0 / ""). Existing remotes keep their rolling_code ; only their name needs a manual rename if it contains characters now rejected by the MQTT-safe regex.
- **Open / Close duration in seconds with one decimal in the UI / MQTT** (`OpenDuration 18.5`). Stored as `uint32_t` milliseconds internally.
- **Position persisted only on motion stop**, not during. Flash-wear-friendly. On boot, last persisted value is the start of any new motion.
- **External operation = silent drift.** No way to detect a physical remote pressed by the user. Document it ; next full Open or Close recalibrates.
- **Stop-at-target logic** : if target is 100 or 0, motor auto-stops at end of travel ; we just transition to idle and snap. If target is 1-99, the orchestrator enqueues a Stop RF emission when the estimated position reaches the target. The motor may overshoot by a few % ; acceptable.
- **1 Hz telemetry during motion** to match Tasmota's cadence and limit broker / WiFi pressure. Plus immediate publish on state changes (motion start, stop, cmd ack).
- **Position-to-N forbidden when `open_time_ms == 0`** : returns a `stat/<topic>/<name>` JSON `{"error":"not calibrated"}`. Open / Close / Stop still work.
