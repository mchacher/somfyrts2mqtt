# 020 — Drop ESP32 WROOM board

## Goal
Drop the dormant `esp32-wroom` build target and every reference to it across
the codebase. The C3 Super Mini is the only board ever shipped in
practice; carrying the WROOM env adds CI build time, doubles release
artifacts, and dilutes the documentation with a variant that is no longer
maintained.

## Scope

In scope:
- `platformio.ini`: remove the `[env:esp32-wroom]` block. `esp32-c3-mini`
  stays as the only firmware env (plus `native` for tests).
- `.github/workflows/ci.yml`: drop `esp32-wroom` from the build matrix.
- `.github/workflows/release.yml`: drop the WROOM build step + the
  matching `cp .pio/build/esp32-wroom/firmware.bin …` artifact line.
- `include/config.h`: remove the `#elif defined(CONFIG_IDF_TARGET_ESP32)`
  CC1101 pinout block (the WROOM pinout). Tidy the `#error` fallback so
  the only supported target is `CONFIG_IDF_TARGET_ESP32C3`.
- `src/wifi_manager.cpp`: drop the stale "Harmless on WROOM" comment
  next to the TX-power clamp.
- `README.md`, `CLAUDE.md`, `docs/hardware.md`, `docs/releasing.md`,
  `docs/troubleshooting.md`, `docs/setup.md`: remove WROOM mentions,
  pinout tables, and "two boards" framing. Keep the C3 Super Mini
  pinout / hardware notes intact.

Out of scope:
- The historical specs under `specs/006-...` through `specs/018-...`
  that mention WROOM — those are append-only project records and stay
  unchanged.
- Removing tooling from `tools/` (none of it is WROOM-specific).
- Any C3-side refactor — pinouts, TX power workaround, captive portal,
  etc. stay exactly as today.

## Acceptance criteria

- [ ] `grep -ri "wroom" src/ include/ .github/ docs/ README.md CLAUDE.md`
      returns zero matches (excluding tests / .pio / .git).
- [ ] `pio run -e esp32-c3-mini` clean, zero warnings.
- [ ] `pio check` clean.
- [ ] `pio test -e native` — 74 cases still pass.
- [ ] `pio run -e esp32-wroom` fails with "Unknown environment" (the env
      no longer exists in `platformio.ini`).
- [ ] CI workflow file is syntactically valid (`gh workflow view` is
      happy) and the matrix has a single entry for `esp32-c3-mini`.
- [ ] Release workflow no longer references the WROOM .bin filename.
