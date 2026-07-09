# Plan 021 — Multi-board build + OTA safety

## Steps

1. **Branch** `feat/esp32-s3-multiboard-ota` from `main`.
2. **`include/config.h`** — keep the S3 pinout block already staged; confirm the
   `#error` fallback lists both targets.
3. **`platformio.ini`** — keep `[env:esp32-s3-picybi]`; add
   `-DFW_VARIANT=\"${PIOENV}\"` to `[firmware_base].build_flags`.
4. **`include/ota_guard.h`** — new pure header:
   - `constexpr uint16_t EXPECTED_CHIP_ID` from `CONFIG_IDF_TARGET_*`.
   - `const char* chip_name(uint16_t id)` → "ESP32-C3" / "ESP32-S3" / "chip 0xNN".
   - `std::optional<uint16_t> header_chip_id(const uint8_t* buf, size_t len)` —
     returns nullopt on `len < 14` or `buf[0] != 0xE9`, else the LE uint16 at
     offset 12. Doxygen per house style.
5. **`src/web_ui.cpp`**:
   - In `handle_firmware_upload`, at `index == 0`, run the guard before
     `Update.begin`. On mismatch, stash an error + short-circuit; the completion
     handler returns 400 with the message. Reuse the existing failure JSON path.
   - Status card: add a `FW_VARIANT` line/span next to the version.
6. **`src/main.cpp`** — wrap the CDC boot-wait in `#if defined(WAIT_FOR_SERIAL)`;
   default build has just `Serial.begin(115200); delay(200);` as before.
7. **`test/test_ota_header/`** — native Unity test: valid C3 header (cid 5),
   valid S3 header (cid 9), bad magic → nullopt, len 13 → nullopt, exact-14 buffer.
8. **`.github/workflows/ci.yml`** — matrix `env: [esp32-c3-mini, esp32-s3-picybi]`
   for build + cppcheck; keep the native `test` job.
9. **`.github/workflows/release.yml`** — build both envs; loop the stage/copy
   over both; keep the 0xE9 sanity + sha256; update the notes body.
10. **Docs** — `docs/hardware.md` (S3 pinout table), `docs/releasing.md` (two
    artifacts), `README.md` (two boards + pick-your-binary), `CLAUDE.md`
    (hardware section: add S3, drop "only C3").
11. **Build / check / test**:
    - `pio run -e esp32-c3-mini` and `pio run -e esp32-s3-picybi` clean.
    - `pio check -e esp32-c3-mini` / `-e esp32-s3-picybi` clean.
    - `pio test -e native` green (incl. `test_ota_header`).
12. **Flash + HW test** on the S3 (test plan below).
13. **PR** on `mchacher/somfyrts2mqtt`; link this spec; HW checklist in the body.

## Test plan (HW — ESP32-S3 PICYBI)

- **Nominal update**: build `esp32-s3-picybi`, upload its `firmware.bin` via the
  admin UI → progress reaches 100 %, board reboots, Status card shows
  `esp32-s3-picybi <version>`. Shutters still controllable afterwards.
- **Wrong-chip rejection**: upload an `esp32-c3-mini` `firmware.bin` via WebOTA
  → UI shows an error like "firmware targets ESP32-C3, this bridge is ESP32-S3",
  HTTP 400, **no reboot**, current firmware keeps running (verify a command
  still works). Expected serial (with `-DWAIT_FOR_SERIAL`): `[ota] reject: chip mismatch ...`.
- **Corrupt image** (regression of spec 016): truncate the correct S3 bin to
  half → `Update.end` fails MD5 → 400, no reboot.

## Edge cases
- [ ] First chunk smaller than 14 bytes (pathological client) → guard returns
      nullopt → treat as "not an ESP image", 400, no write.
- [ ] `pio run -e esp32-wroom` still fails "Unknown environment" (we did not
      resurrect WROOM — only added S3).
- [ ] C3 build unchanged: `FW_VARIANT` string present but no behaviour change;
      no CDC wait by default.
- [ ] CI YAML valid after matrix change (`gh workflow view`).
