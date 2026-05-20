# 008 — multi-board support (ESP32 WROOM + ESP32-C3)

## Goal
Make the firmware buildable and flashable on both the ESP32-C3 Super Mini (original target) and a classic ESP32 WROOM board (DevKit / NodeMCU-32S). Same source tree, two PIO envs, pinout selected at compile time.

Triggered by the C3 Super Mini being damaged in iter 005's HW debugging; the project must keep moving on the spare NodeMCU-32S available.

## Scope

**In scope:**
- New PIO env `esp32-wroom` (board `esp32dev`) alongside the existing `esp32-c3-mini`.
- `include/config.h` switches CC1101 pin numbers based on the target chip (`CONFIG_IDF_TARGET_ESP32C3` vs `CONFIG_IDF_TARGET_ESP32`).
- CI builds + cppcheck both envs (matrix). Native tests stay on `native`.
- `CLAUDE.md` documents both pinouts.

**Out of scope:**
- ESP32-S2 / S3 / H2 support (will reuse the same pattern when needed).
- Per-board runtime selection (would need extra config; we trust the build env).
- Re-targeting `default_envs` — keeps `esp32-c3-mini` as the default. User explicitly passes `-e esp32-wroom` when they want the WROOM build.

## Acceptance criteria
- [ ] `pio run -e esp32-c3-mini` and `pio run -e esp32-wroom` both build clean (zero warnings).
- [ ] `pio check -e esp32-c3-mini` and `pio check -e esp32-wroom` both pass (zero defects).
- [ ] `pio test -e native` still passes (no change to native).
- [ ] CI runs both envs in parallel via a matrix; PR shows two green `Build, check, test (<env>)` jobs.
- [ ] WROOM boot test: flashed firmware on the NodeMCU-32S logs the same sequence as the C3 (boot → nvs → rf stub init → wifi → mqtt → web ui).
- [ ] CLAUDE.md pinout section covers both boards.

## Decisions
- **`extends` + `[firmware_base]`** in `platformio.ini` to share the bulk of config between the two envs (lib_deps, check_*, etc.) without copy-paste.
- **`CONFIG_IDF_TARGET_*` macros** (from ESP-IDF) drive the conditional in `config.h`. They're set by the framework automatically; no extra build_flag needed.
- **CI matrix** rather than two duplicated jobs: simpler to read, scales if we add a 3rd board later.
- **VSPI default pins on WROOM** (SCK=18, MISO=19, MOSI=23) so user docs match common ESP32 tutorials. CSN, GDO0, GDO2 on free non-strapping pins (21, 22, 17).
