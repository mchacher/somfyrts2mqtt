# Plan 022 — RTS device types (foundation + Gate)

## Steps

1. **Branch** `feat/device-types-gate` from `main`.
2. **`include/device_profile.h`** (new, pure):
   - `enum class DeviceType : uint8_t { Shutter = 0, Gate = 1 };`
   - `inline bool uses_position(DeviceType)` → true only for `Shutter`.
   - `inline const char* name(DeviceType)` → `"shutter"` / `"gate"`.
   - `inline DeviceType from_u8(uint8_t)` → unknown ⇒ `Shutter`.
   - Doxygen per house style.
3. **`nvs_store`**:
   - `Remote.device_type` (uint8, default 0); `remote_type_key(hex)`.
   - `add_remote` seeds `remote_type_<hex> = 0`; `get_remote`/`list_remotes`
     read with default 0; `delete_remote` removes the key.
   - `bool set_device_type(uint32_t id, uint8_t type)`.
   - **`SCHEMA_VERSION` stays 1** — additive key; a bump would trip `init()`'s
     "refusing to operate" on upgraded boxes. Update the comment to note iter 022.
4. **`orchestrator`** — in `handle_command`, compute the profile and branch step 5:
   - positional ⇒ existing state-machine block, unchanged;
   - non-positional ⇒ binary set (Up→100 / Down→0 / Stop→noop) + persist.
   - `tick()` skips non-positional remotes; `set_position` maps 100→Open /
     0→Close for non-positional, warns on other values; duration setters warn+ignore.
5. **`mqtt`** — in `publish_sensor_aggregated` + `publish_shutter_state`, add
   `o["Type"] = device_profile::name(...)` **guarded by `type != Shutter`**.
   Buffer size already 512 (headroom OK).
6. **`web_ui`** (admin portal — extra care on layout/UX):
   - Setup row: a `<select class="dtype">` (Shutter / Gate) above the invert
     line; commit via `POST /api/remotes/<id>/type/<n>` (extend the numeric
     setter regex + `handle_post_value` with a `type` action).
   - JS: when type != shutter, hide the duration/position calibration inputs so
     the row stays clean and only shows what applies to a gate.
   - `/api/remotes` GET JSON gains `"device_type"`.
7. **Tests**:
   - `test/test_device_profile/` — mapping, `uses_position`, `from_u8` fallback
     (this fully covers the branch *decision*).
   - The `handle_command` Gate branch and `tick()` skip are HW-validated (the
     orchestrator's end-to-end path needs NVS/MQTT/RF; `test_orchestrator`
     already only unit-tests the pure helpers). Covered by the HW test plan.
8. **Docs** — `docs/web-ui.md` (Type selector), `docs/mqtt-api.md` (the additive
   `Type` field + gate semantics), `README.md` + `CLAUDE.md` (device types).
9. **Build / check / test** — `pio run` both envs, `pio check` both, `pio test
   -e native` green.
10. **Flash + HW test** on a bench remote (below).
11. **PR** on `mchacher/somfyrts2mqtt`; link this spec; HW checklist in the body.

## Test plan (HW)

- **Non-regression (critical)**: an existing Shutter remote — Open/Close/Stop +
  Position N still work; the SENSOR JSON for that remote is unchanged (no `Type`
  field). Compare an `mosquitto_sub` capture before/after the flash.
- **Gate nominal**: set a remote to `Gate` in the UI; `Open` emits Up and
  reports `"Type":"gate"`, Position 100; `Close` → Position 0; `Stop` emits My.
  No auto-stop tick fires; the calibration inputs are hidden.
- **Persistence**: reboot → the remote is still `Gate` (NVS survived); a remote
  left as Shutter is still Shutter.
- **Upgrade compat (critical)**: OTA from v0.4.0 onto a box with existing shutter
  remotes → NVS still opens (schema unchanged, no "refusing to operate"); all
  remotes retain their config and Shutter behaviour (device_type defaulted to 0).

## Edge cases
- [ ] Unknown/forward `device_type` value in NVS ⇒ `from_u8` ⇒ Shutter (no crash).
- [ ] `Position`/`OpenDuration` sent to a Gate remote ⇒ warn + no state break.
- [ ] SENSOR buffer still fits with the extra `Type` field on a full remote table.
- [ ] `invert` + `Gate` compose (Up/Down swap still applies at RF layer).
