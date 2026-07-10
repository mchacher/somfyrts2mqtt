# Plan 022 — RTS device types (foundation + Gate)

## Steps

1. **Branch** `feat/device-types-gate` from `main`.
2. **`include/device_profile.h`** (new, pure): `DeviceType {Shutter=0, Gate=1}`;
   `uses_position` (true only for Shutter); `name` ("shutter"/"gate"); `from_u8`
   (unknown ⇒ Shutter); toggle-button codes `TOGGLE_BTN_MY/UP/DOWN` +
   `valid_toggle_button` (clamp to My/Up/Down, default Up).
3. **`nvs_store`**: `Remote.device_type` + `Remote.toggle_button`;
   `remote_type_key` (".typ") + `remote_toggle_key` (".tgb"); seed in
   `add_remote` (type 0, button 0x02); read in `get_remote`/`list_remotes`;
   remove on delete; `set_device_type` / `set_toggle_button`. **SCHEMA stays 1.**
4. **`mqtt`**: add `Command::Toggle` (+ `command_to_str`); a `Toggle` cmnd verb;
   emit `"Type"` in SENSOR + stat guarded by `type != Shutter`.
5. **`orchestrator`**: resolve the button (Toggle → `valid_toggle_button`, no
   invert; else mapped/inverted). Shutter keeps the state machine; a Gate updates
   no runtime state. `tick()` skips non-positional; `set_position`/duration
   setters warn+ignore for a Gate.
6. **`web_ui`** (admin portal — extra care): Type `<select>` + a Toggle-button
   `<select>` (gate-only); the Gate main row shows a single **Toggle** (🔄)
   button + a blind "—" state; duration/position/invert hide for a Gate.
   `/api/remotes/<id>/toggle` command + `/type/<n>` + `/toggle_button/<n>`
   setters; `/api/remotes` JSON gains `device_type` + `toggle_button`.
7. **Tests**: `test/test_device_profile/` — mapping, `uses_position`, `from_u8`,
   `valid_toggle_button`. The `handle_command` Toggle path is HW-validated
   (`test_orchestrator` only unit-tests the pure helpers — end-to-end needs
   NVS/MQTT/RF).
8. **Docs** — `docs/mqtt-api.md` (Toggle verb + gate semantics + `Type`),
   `docs/web-ui.md` (Type + Toggle button), `README.md`, `CLAUDE.md`.
9. **Build / check / test** — `pio run` both envs, `pio check` both, `pio test
   -e native` green.
10. **Flash + HW test** on a bench gate (below).
11. **PR** on `mchacher/somfyrts2mqtt` (revises PR #36); link this spec.

## Test plan (HW)

- **Non-regression (critical)**: an existing Shutter remote — Open/Close/Stop +
  Position N still work; its SENSOR JSON is unchanged (no `Type`). No re-pairing.
- **Gate toggle**: set a remote to `Gate`; the UI shows a single Toggle button +
  a Toggle-button selector. Clicking **Toggle** (or `cmnd/.../Toggle`) emits the
  configured button once → the gate advances one step (open → stop → close →
  stop). Try the three button choices if the default (Up) does not cycle.
- **No re-pairing**: after OTA from v0.4.0 + switching the remote to Gate, the
  already-paired motor responds (remote_id + rolling_code preserved).
- **Persistence**: reboot → type + toggle button survive; other remotes stay Shutter.

## Edge cases
- [ ] Unknown/forward `device_type` ⇒ `from_u8` ⇒ Shutter (no crash).
- [ ] `toggle_button` = 0/junk ⇒ `valid_toggle_button` ⇒ Up.
- [ ] `Position`/`OpenDuration` sent to a Gate ⇒ warn + ignore.
- [ ] SENSOR buffer still fits with the extra `Type` field.
