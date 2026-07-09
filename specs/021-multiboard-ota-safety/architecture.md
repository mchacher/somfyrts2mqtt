# Architecture 021 — Multi-board build + OTA safety

## Touched modules

| File                              | Change                                                                                  |
| --------------------------------- | --------------------------------------------------------------------------------------- |
| `include/config.h`                | Add S3 CC1101 pinout block; broaden the `#error`; `FW_VARIANT` fallback define.         |
| `platformio.ini`                  | Add `[env:esp32-s3-picybi]`.                                                            |
| `scripts/release_version.py`      | Inject `-DFW_VARIANT="<env>"` from `env["PIOENV"]` (alongside `FW_VERSION`).            |
| `include/ota_guard.h`             | **New.** Pure header: `header_chip_id()` + `EXPECTED_CHIP_ID` + `chip_name()`.          |
| `src/web_ui.cpp`                  | First-chunk chip guard in the OTA upload handler; `FW_VARIANT` in the Status card.      |
| `src/main.cpp`                    | Temp CDC wait → `#if defined(WAIT_FOR_SERIAL)` guard (default off).                      |
| `.github/workflows/ci.yml`        | Build + cppcheck both envs (matrix).                                                     |
| `.github/workflows/release.yml`   | Build both envs; stage + publish both `.bin`; adjust release-notes body.                |
| `test/test_ota_header/`           | **New.** Native unit tests for `ota_guard`.                                             |
| `docs/*`, `README.md`, `CLAUDE.md`| Re-introduce the two-board framing + S3 pinout + pick-your-binary notes.                |

## Decisions

**Guard on `chip_id`, parsed from the image header, at the first OTA chunk.**
The `esp_image_header` sits at offset 0 of every ESP app image: byte 0 is the
magic `0xE9`, and `chip_id` (uint16, little-endian) is at offset 12
(`ESP_CHIP_ID_ESP32C3 = 5`, `ESP_CHIP_ID_ESP32S3 = 9`). The first WebOTA chunk
is far larger than 14 bytes, so we can validate before calling `Update.begin()`
/ `Update.write()` and fail fast with a precise message. *Rejected: relying on
the bootloader's post-reboot `chip_id` check alone* — it prevents a brick but
surfaces to the user as an unexplained rollback to the old version.

**`ota_guard` is a pure header, no `Update.h` / Arduino dependency.** Keeps the
parsing unit-testable from the `native` env (same pattern as
`include/shutter_state.h`). `web_ui.cpp` supplies the compile-time expected id
and calls the pure comparison; the header itself never touches hardware.

**`FW_VARIANT` = the env name, injected by the pre-script** (`env["PIOENV"]`
in `scripts/release_version.py`), mirroring how `FW_VERSION` is injected — not
via `${PIOENV}` interpolation in `platformio.ini`, which is not reliable for
the env name. The env name is the same token that disambiguates the release
filename, so what the user sees in the Status card matches the file they
downloaded. A `#ifndef FW_VARIANT` fallback in `config.h` keeps any build
without the pre-script compiling.

**CDC boot-wait becomes opt-in (`-DWAIT_FOR_SERIAL`), not shipped on.** A fixed
multi-second wait on every boot would slow power-loss recovery for a
"set-and-forget" bridge. Native-USB boards (C3/S3) lose the first serial lines
to USB re-enumeration; a developer who needs them builds with the flag. Default
production boot is unchanged.

**`chip_id` guard does not cover same-chip / different-pinout variants.**
Explicitly out of scope; a `FW_VARIANT` string check against the incoming
`esp_app_desc` is the documented follow-up if a second same-chip board appears.

## Expected chip id (compile-time)

```
CONFIG_IDF_TARGET_ESP32C3 → EXPECTED_CHIP_ID = 0x0005  ("ESP32-C3")
CONFIG_IDF_TARGET_ESP32S3 → EXPECTED_CHIP_ID = 0x0009  ("ESP32-S3")
```

## Flow (WebOTA upload)

```
POST /api/firmware/upload  (multipart, streamed)
  chunk index==0:
      len >= 14 && data[0]==0xE9 ?           no  → 400 "not an ESP image"
      cid = data[12] | data[13]<<8
      cid == EXPECTED_CHIP_ID ?               no  → 400 "firmware targets <X>, this bridge is <Y>"
      Update.begin(UPDATE_SIZE_UNKNOWN)
  chunk index>0: Update.write(chunk)
  final:          Update.end(true)  // MD5 verified, boot partition flipped
  complete:       200 {ok,rebooting} → delay(500) → ESP.restart()
```

No partition-table change (`min_spiffs.csv` already has `app0`/`app1`/`otadata`).
