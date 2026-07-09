# 021 — Multi-board build + OTA safety (ESP32-S3 PICYBI)

## Goal
Add the ESP32-S3 "PICYBI Radio Remote" as a second supported board (its own
CC1101 pinout + PlatformIO env), and make the build / release / OTA pipeline
safe now that more than one firmware binary exists — so a user can never brick
or silently mis-flash a board by uploading the wrong `.bin` over WebOTA.

## Background
The pinout and chip target are compile-time (`include/config.h`, selected by
`CONFIG_IDF_TARGET_*`). A C3 binary and an S3 binary are therefore not
interchangeable: different ISA (RISC-V vs Xtensa), different bootloader,
different GPIOs. Today `release.yml` ships a single `esp32-c3-mini` binary and
the WebOTA handler validates only the embedded MD5 (integrity), not that the
image matches the running board. A wrong-chip image is caught by the 2nd-stage
bootloader (`chip_id` mismatch → dual-app rollback, no brick) but the user only
sees a confusing silent revert. We want an explicit, early rejection and a
release that actually offers the right binary per board.

## Scope

In scope:
- `include/config.h`: add the `#elif defined(CONFIG_IDF_TARGET_ESP32S3)` CC1101
  pinout block (SCK12 / MISO13 / MOSI11 / CSN10 / GDO0 8 / GDO2 9); update the
  `#error` fallback to list both supported targets. *(already staged in the
  working tree from the validation flash)*
- `platformio.ini`: add `[env:esp32-s3-picybi]` (already staged) and add
  `-DFW_VARIANT=\"${PIOENV}\"` to `[firmware_base]` so every binary embeds its
  env name.
- `include/ota_guard.h`: new pure-logic header — parse the `chip_id` out of an
  `esp_image_header` prefix and expose the compile-time expected id. Native-
  testable, no Arduino/Update dependency.
- `src/web_ui.cpp`:
  - WebOTA upload handler rejects a wrong-chip image on the first chunk with
    HTTP 400 and a human message ("firmware targets ESP32-C3, this bridge is
    ESP32-S3"), before writing anything.
  - Status card displays `FW_VARIANT` next to `FW_VERSION`.
- `.github/workflows/ci.yml`: build + cppcheck **both** envs.
- `.github/workflows/release.yml`: build both envs, stage both bins as
  `somfyrts2mqtt-<ver>-<env>.bin`, update the release-notes body to tell the
  user to pick the binary matching their board.
- `src/main.cpp`: replace the temporary unconditional USB-CDC boot wait with a
  compile-guarded `#if defined(WAIT_FOR_SERIAL)` block (default off — no boot
  penalty in production; opt-in for native-USB serial debugging).
- `test/test_ota_header/`: native unit tests for `ota_guard` header parsing.
- Docs: `docs/hardware.md` (S3 board + pinout table), `docs/releasing.md`
  (two bins), `docs/setup.md` (per-board flash + OTA), `docs/web-ui.md`
  (Variant field + wrong-chip guard), `docs/troubleshooting.md` (S3 boot-log
  note), `README.md` (two supported boards / pick-your-binary), `CLAUDE.md`
  (re-add S3 to the hardware section; the "only C3" wording from spec 020 no
  longer holds).

Out of scope:
- Same-chip / different-pinout variant discrimination (a second S3 board with a
  different mapping). The `chip_id` guard does not cover it; a `FW_VARIANT`
  marker check in the image `esp_app_desc` is the follow-up if that ever ships.
- Auto-update / GitHub Releases polling, code-signing, HTTPS OTA — unchanged
  from spec 016 out-of-scope.
- Any C3-side behaviour change. The C3 binary stays functionally identical
  (only the new `FW_VARIANT` string and the CDC-wait guard differ, both inert
  at runtime on C3).

## Acceptance criteria
- [ ] `pio run -e esp32-c3-mini` and `pio run -e esp32-s3-picybi` both clean, zero warnings.
- [ ] `pio check` clean on both envs (high + medium).
- [ ] `pio test -e native` green, including new `test_ota_header` cases
      (valid C3 header, valid S3 header, bad magic, truncated buffer).
- [ ] HW (S3): upload the correct `esp32-s3-picybi` bin via WebOTA → progress
      100 %, reboot, Status card shows `esp32-s3-picybi <version>`.
- [ ] HW (S3): upload an `esp32-c3-mini` bin via WebOTA → rejected with 400 and
      a clear "wrong chip" message, board keeps running the old firmware (no reboot).
- [ ] CI builds both envs; a pushed `v*` tag produces a release listing
      `somfyrts2mqtt-<ver>-esp32-c3-mini.bin` **and**
      `somfyrts2mqtt-<ver>-esp32-s3-picybi.bin`.
- [ ] Production boot time on the C3 is unchanged (no CDC wait by default).
- [ ] CI green on the PR.
