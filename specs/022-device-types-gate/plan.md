# Plan 022 — RTS device types (foundation + Gate)

## Steps

1. **Branch** `feat/device-types-gate` from `main`.
2. **`include/device_profile.h`** (new, pure): `DeviceType {Shutter=0, Gate=1}`;
   `uses_position` (true only for Shutter); `name` ("shutter"/"gate"); `from_u8`
   (unknown ⇒ Shutter); `SOMFY_TOGGLE = 0x0C` (dedicated Somfy Toggle command).
3. **`nvs_store`**: `Remote.device_type`; `remote_type_key` (".typ"); seed in
   `add_remote` (type 0); read in `get_remote`/`list_remotes`; remove on delete;
   `set_device_type`. **SCHEMA stays 1.**
4. **`mqtt`**: add `Command::Toggle` (+ `command_to_str`); a `Toggle` cmnd verb;
   emit `"Type"` in SENSOR + stat guarded by `type != Shutter`.
5. **`orchestrator`**: resolve the button (Toggle → `0x0C`, no invert; else
   mapped/inverted). Shutter keeps the state machine; a Gate updates no runtime
   state. `tick()` skips non-positional; `set_position`/duration setters
   warn+ignore for a Gate.
6. **`web_ui`** (admin portal — extra care): Type `<select>`; the Gate command
   cell shows **Open / Stop / Close + a Toggle (🔄)** button and a blind "—"
   state; duration/position/invert hide for a Gate. `/api/remotes/<id>/toggle`
   command + `/type/<n>` setter; `/api/remotes` JSON gains `device_type`.
7. **Tests**: `test/test_device_profile/` — mapping, `uses_position`, `from_u8`,
   `SOMFY_TOGGLE`. The `handle_command` Toggle path is HW-validated
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
- **Gate toggle**: set a remote to `Gate`; the command cell shows Open / Stop /
  Close + a **Toggle** button. Clicking **Toggle** (or `cmnd/.../Toggle`) emits
  the Somfy Toggle command `0x0C` → the gate advances one step (open → stop →
  close → stop). The RF log shows `button=0x0C`. Open/Close/Stop still emit
  Up/Down/My.
- **No re-pairing**: after OTA from v0.4.0 + switching the remote to Gate, the
  already-paired motor responds (remote_id + rolling_code preserved).
- **Persistence**: reboot → type + toggle button survive; other remotes stay Shutter.

## Edge cases
- [ ] Unknown/forward `device_type` ⇒ `from_u8` ⇒ Shutter (no crash).
- [ ] `Toggle` sent to a Shutter ⇒ emits `0x0C` too (harmless; motor ignores it).
- [ ] `Position`/`OpenDuration` sent to a Gate ⇒ warn + ignore.
- [ ] SENSOR buffer still fits with the extra `Type` field.
