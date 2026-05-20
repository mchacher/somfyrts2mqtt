# Plan 008

## Steps

1. Refactor `platformio.ini`: introduce `[firmware_base]` with the shared options; make `[env:esp32-c3-mini]` `extends = firmware_base`; add a new `[env:esp32-wroom]` that also extends the base.
2. Update `include/config.h`: wrap the `CC1101_*` pin defines in `#if CONFIG_IDF_TARGET_ESP32C3` / `#elif CONFIG_IDF_TARGET_ESP32` / `#else #error`.
3. Update `.github/workflows/ci.yml`: switch to a matrix build over `[esp32-c3-mini, esp32-wroom]`; run native tests once.
4. Update `CLAUDE.md`: split the pinout section into "ESP32-C3 Super Mini" and "ESP32 WROOM (NodeMCU-32S / DevKit)".
5. Run `pio run -e esp32-c3-mini` and `pio run -e esp32-wroom` locally; both must build clean.
6. Run `pio check -e esp32-c3-mini` and `pio check -e esp32-wroom`; both zero defects.
7. Run `pio test -e native`; still green.

## Test plan

### Native (Unity)

No change. The existing 26 tests (smoke + nvs + mqtt + orchestrator) still pass.

### Build matrix

| Env | Expected |
|---|---|
| `esp32-c3-mini` | Build SUCCESS, no warnings, same flash usage as iter 004 (~36% on the C3 partition table) |
| `esp32-wroom` | Build SUCCESS, no warnings, flash usage similar (~36% on the WROOM partition table) |
| `native` | 26 cases PASSED |

### HW (NodeMCU-32S)

| Case | Action | Expected |
|---|---|---|
| **WROOM boot** | Flash with `pio run -e esp32-wroom -t upload`, monitor serial | Same boot sequence as before: `[boot] ... v0.1.0` → `[nvs] ready` → `[rf] stub init` → `[wifi] connected ip=...` → `[web] listening on http://...` → `[mqtt] connected` |
| **Web UI on WROOM** | Browse the IP in a browser | Page renders, same UI as the C3 |
| **MQTT dispatch on WROOM** | `mosquitto_pub -t somfy2mqtt/A1B2C3/set -m up` (after adding the remote via the UI) | Same chain: `[mqtt] cmd ... [rf] STUB tx ... [orch] executed ...`. Retained state + rolling_code appear on the broker. |

The C3 HW test is unchanged from iter 004 — the source diff doesn't touch firmware behavior.
