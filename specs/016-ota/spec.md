# 016 — OTA firmware update (WebOTA + ArduinoOTA)

## Goal
Let the user update the bridge firmware over WiFi, without ever plugging in USB again after the initial commissioning. Two complementary paths :

- **WebOTA** — drag-and-drop a `firmware.bin` from the admin UI. End-user friendly, no extra tooling.
- **ArduinoOTA** — `pio run -t upload --upload-port somfyrts2mqtt.local` from PlatformIO. Developer workflow, ~10 lines of code in addition.
- **GitHub Releases** — every tagged version (`v0.2.0`, `v0.2.1`, …) ships a downloadable `firmware.bin` per board, attached to the release page. Users grab the binary from <https://github.com/mchacher/somfyrts2mqtt/releases>, upload it via WebOTA. Built by CI on tag push.

## Background
Today the only update path is USB + `pio run -t upload`. Once the bridge is wired in place behind a shutter or in a junction box, that becomes painful. ESP32 supports OTA natively via the dual-app partition scheme and the `Update` library ; we already use the `min_spiffs.csv` partition table which ships two ~1.9 MB OTA slots (`app0` / `app1`), so A/B rollback is gratuitous : the chip boots from the partition that successfully completed `Update.end()` ; on a failed write the previous one stays active.

## Scope

**In scope:**

### WebOTA
- New section in the admin UI : **Update** (placed in the Danger zone area, next to Factory reset).
- File input + Upload button + progress bar. Accepts `.bin` only ; max size = the free OTA slot (~1.9 MB).
- `POST /api/firmware/upload` multipart handler in `web_ui.cpp` using the `AsyncCallbackJsonWebHandler`-equivalent for binary uploads (`s_server.on(..., HTTP_POST, ..., onUpload)`).
- Backend uses `Update.begin(UPDATE_SIZE_UNKNOWN)` → `Update.write(buf, len)` chunks → `Update.end(true)`. The lib validates the embedded MD5 and rejects truncated / corrupted binaries.
- On success : log, return 200 with `{"ok":true,"rebooting":true}`, defer a 500 ms `delay()` and `ESP.restart()` so the response actually flushes.
- On failure : 400 with `{"error":"<Update.errorString()>"}`. No partition switch happens ; the current firmware keeps running.
- Two-step confirm dialog in the UI (same pattern as Factory reset).
- Visible firmware version in the Status card (already there since iter 001), so the user can verify the new version after reboot.

### ArduinoOTA (dev workflow bonus)
- Enable in `wifi_manager.cpp` once `STA_GOT_IP` fires (alongside mDNS).
- `ArduinoOTA.setHostname(WiFi.getHostname())` so PlatformIO sees `somfyrts2mqtt.local`.
- `ArduinoOTA.setPassword(OTA_PASSWORD)` — compile-time string from `config.h`, default value clearly marked "change me", short doc note.
- `ArduinoOTA.begin()` + `ArduinoOTA.handle()` in `loop()`.
- mDNS service advertise `_arduino._tcp` on port 3232 (the default ArduinoOTA port) so PlatformIO discovers the bridge automatically.

### GitHub Releases publishing
- New CI job in `.github/workflows/release.yml` (or extend the existing one) triggered on `push: tags: ['v*']`.
- Steps : checkout, set up PlatformIO, `pio run -e esp32-c3-mini` + `pio run -e esp32-wroom`, take the resulting `.pio/build/<env>/firmware.bin`, rename to `somfyrts2mqtt-<version>-<env>.bin`, create a GitHub Release with both binaries attached and an auto-generated body (commit messages since the previous tag or a manual `CHANGELOG.md` block).
- Versioning : the `FW_VERSION` macro in the firmware (currently `0.1.0`) should match the git tag. A small `scripts/release.sh` could bump both atomically ; for now manual bump + tag is fine.
- Releases are marked "latest" automatically by GitHub. Pre-releases (`v0.2.0-rc1`) flagged as such via the tag name.

**Out of scope:**
- HTTPS / code-signed updates. The bridge is LAN-only ; any peer on the LAN that hits the admin UI can already operate the shutters. Adding signature verification is a separate hardening iter if ever needed.
- Auto-update from a remote URL / GitHub Releases poll. Single-bridge users do not need fleet management. Can be added in a future iter for multi-bridge / kiosk deployments.
- SPIFFS / LittleFS data partition updates. The firmware does not store user data in a filesystem partition (all in NVS) ; no need to push a `littlefs.bin`.
- Rollback button in the UI. The dual-app A/B partition scheme already handles failed updates ; an explicit "go back to the previous firmware" requires user-space tracking that we do not need yet.
- OTA from MQTT (`cmnd/.../Update` with a URL). Out of scope ; can be added if Sowel wants centralised updates.

## Acceptance criteria
- [ ] Build clean on `esp32-c3-mini` and `esp32-wroom`. `pio check` zero defects, `pio test -e native` all green.
- [ ] HW : take a `firmware.bin` produced by `pio run`, upload it via the admin UI's Update section. Progress bar reaches 100 %, bridge reboots, Status card shows the new version.
- [ ] HW : intentionally truncate the binary to half its size, upload → backend rejects with 400 `{"error":"MD5 mismatch"}` (or equivalent), bridge keeps running the old firmware (no reboot).
- [ ] HW : `pio run -t upload --upload-port somfyrts2mqtt.local --upload-flags "--auth=<password>"` succeeds. Bridge reboots into the new firmware.
- [ ] HW : wrong ArduinoOTA password → upload rejected, bridge keeps running.
- [ ] CI : push a `v0.2.0` tag → release workflow runs → release page lists `somfyrts2mqtt-0.2.0-esp32-c3-mini.bin` and `somfyrts2mqtt-0.2.0-esp32-wroom.bin` as downloadable assets.
- [ ] Documentation : `docs/setup.md` and `docs/web-ui.md` gain a "Update from a GitHub Release" section. README quickstart links to the Releases page as the user-facing source of binaries.
- [ ] CI green on the PR.

## Decisions
- **Update via existing AsyncWebServer.** Not a new HTTPServer instance. Keeps the firmware compact and matches the rest of the admin UI.
- **No JSON body, multipart upload.** AsyncWebServer's `onUpload` callback streams chunks straight to `Update.write()` — never holds the full binary in RAM.
- **No size pre-flight.** `Update.begin(UPDATE_SIZE_UNKNOWN)` accepts any size up to the partition limit ; the IDF will fail the `end()` if the slot is overflowed. Keeps the UI / backend contract simple.
- **Two-step confirm in the UI**, same pattern as Factory reset. OTA bricks the device on a botched binary ; a single misclick must not trigger it.
- **Dev OTA password in `config.h`** with a clearly marked default. Treated like the legacy WIFI_PASSWORD default : developers can set their own locally, no friction for the captive-portal-commissioned path (ArduinoOTA is dev-only ; production users should not need it).
- **mDNS advertises both services.** `_http._tcp` (iter 015) + `_arduino._tcp` (this iter). PlatformIO + browsers discover the bridge under one mDNS name.
- **No partition table change.** `min_spiffs.csv` already has `app0` + `app1` + `otadata`. Verified.
- **Releases shipped via GitHub Actions, not from a developer's laptop.** Reproducible builds, no "works on my machine", and the CI matrix already runs on every PR -- adding a release-on-tag job is mostly YAML.
- **No code-signing of release binaries** for now. Same rationale as the WebOTA upload : LAN-only attack surface. A future iter can add SHA256 sums + checksum verification in the WebOTA UI if needed.
