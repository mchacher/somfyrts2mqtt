# Plan 020 — Drop ESP32 WROOM board

## Steps

1. **Branch** `chore/drop-wroom-board` from `main`.
2. **`platformio.ini`** — remove the `[env:esp32-wroom]` block + the
   trailing comment above it. Keep `default_envs = esp32-c3-mini`.
3. **`.github/workflows/ci.yml`** — strip `esp32-wroom` from the build
   matrix `env: [...]` line.
4. **`.github/workflows/release.yml`** — drop the WROOM build step + the
   `cp .pio/build/esp32-wroom/firmware.bin ...` artifact copy.
5. **`include/config.h`** — remove the
   `#elif defined(CONFIG_IDF_TARGET_ESP32)` block (the WROOM pinout).
   Update the `#error` fallback message to reference C3 only.
6. **`src/wifi_manager.cpp`** — drop the "Harmless on WROOM" inline
   comment.
7. **`README.md`** — remove WROOM mentions in supported-hardware /
   install snippets. Keep the C3 Super Mini narrative.
8. **`CLAUDE.md`** — remove WROOM in the HW context and pinout
   reminders sections.
9. **`docs/hardware.md`** — remove the WROOM pinout table + any
   "alternative board" framing.
10. **`docs/releasing.md`** — remove the WROOM bin from the listed
    artifacts.
11. **`docs/troubleshooting.md`** — remove the WROOM-specific entry.
12. **`docs/setup.md`** — remove the WROOM step.
13. **Build / static check / native tests**:
    - `~/.platformio/penv/bin/pio run -d . -e esp32-c3-mini` clean.
    - `~/.platformio/penv/bin/pio check -d .` clean.
    - `~/.platformio/penv/bin/pio test -d . -e native` 74 / 74.
14. **Final grep** to confirm no leftover WROOM references in active
    paths:
    `grep -ri "wroom" src/ include/ .github/ docs/ README.md CLAUDE.md`
    should return empty.
15. **PR** on `mchacher/somfyrts2mqtt`.

## Test plan (HW)

No hardware test required — this is a pure scope reduction with no
runtime impact on the supported C3 board. Sanity check on the bench
after merge:

- [ ] Existing C3 firmware OTA'd from a v0.3.x release continues to
      boot, associate WiFi, connect MQTT.
- [ ] The next release after this PR ships a single
      `somfyrts2mqtt-<v>-esp32-c3-mini.bin` artifact and the GitHub
      release body no longer lists a WROOM bin.

## Edge cases

- [ ] CI workflow YAML stays syntactically valid after the matrix
      reduction (`gh workflow view` is happy).
- [ ] `pio run -e esp32-wroom` fails with the explicit
      "Unknown environment" error — confirms removal.
- [ ] `tools/` scripts (if any are board-aware) keep working — verify
      `pio device list` based scripts don't try `esp32-wroom`.
