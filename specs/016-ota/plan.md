# Plan 016

## Steps

The work splits into 4 independent chunks. Each chunk is buildable and shippable on its own.

1. **ArduinoOTA wiring** — ~15 lines.
   - Add `OTA_PASSWORD` to `include/config.h` with a "change me locally" default + a `.gitignored` `secrets.h` override.
   - In `wifi_manager.cpp` STA_GOT_IP handler : `ArduinoOTA.setHostname(...)` + `setPassword(...)` + `begin()` + log on start / progress / end / error.
   - In `wifi::loop()` : `ArduinoOTA.handle()`.
   - Add `[env:esp32-c3-mini-ota]` to `platformio.ini` (extends the regular env with `upload_protocol = espota`).
   - HW test : flash via USB first (since the bridge is not yet OTA-capable), then `pio run -e esp32-c3-mini-ota -t upload --upload-flags "--auth=..."`. Bridge accepts the new firmware.

2. **WebOTA backend** — ~50 lines in `web_ui.cpp`.
   - `s_server.on("/api/firmware/upload", HTTP_POST, ...completion..., ...upload...)`.
   - The upload callback streams chunks straight to `Update.write()`. First chunk : `Update.begin(UPDATE_SIZE_UNKNOWN)` ; last chunk : `Update.end(true)`.
   - Completion handler sends `200 {"ok":true}` then `delay(500)` + `ESP.restart()`.
   - Failure paths : `Update.printError(Serial)` + 400 with the error message.
   - Two-step `confirm()` dialogs in the JS handler before the upload starts.

3. **WebOTA UI** — ~40 lines of HTML / JS in `web_ui.cpp`'s `INDEX_HTML`.
   - New `<section><h2>Update</h2>...</section>` between WiFi and Remotes.
   - File picker + submit ; XHR with progress events ; `<progress>` bar ; success / error message ; auto-poll `/api/status` after the upload to detect the reboot.
   - Link to the GitHub Releases page.

4. **GitHub Actions release workflow** — `.github/workflows/release.yml`.
   - Trigger : `push: tags: ['v*']`.
   - Steps : checkout, install PlatformIO, `pio run -e esp32-c3-mini -e esp32-wroom`, rename binaries to `somfyrts2mqtt-<version>-<env>.bin`, attach to a GitHub Release.
   - Auto-generate release notes from commit messages since previous tag.
   - Documentation update : README quickstart + `docs/setup.md` "Update from a GitHub Release" section + `docs/web-ui.md` Update card walkthrough.

## Test plan

### Native (Unity)
- No new pure-logic helpers. Skip native tests for this iter (lib calls all the way through).

### HW (browser + PlatformIO)

Preconditions : bridge running on the current firmware, accessible at `somfyrts2mqtt.local`.

| Case | Action | Expected |
|---|---|---|
| **WebOTA happy path** | Build a new firmware with a bumped `FW_VERSION` to e.g. `0.2.0`. Browse to the bridge → Update section → pick the new `.bin` → confirm twice → Upload | Progress bar fills, "Rebooting" message, page reload after ~5 s, Status card now shows `0.2.0` |
| **WebOTA truncated binary** | Pick a `.bin` truncated to half size | 400 `{"error":"MD5 mismatch"}`. Status card stays on the old version. No reboot |
| **WebOTA wrong file (PNG)** | Pick a non-binary file | 400 `{"error":"Magic byte is wrong, not 0xE9"}` (or similar). No partition switch |
| **ArduinoOTA dev push** | `pio run -e esp32-c3-mini-ota -t upload --upload-flags "--auth=<password>"` | New firmware flashed, bridge reboots |
| **ArduinoOTA wrong password** | Same with a bogus `--auth=...` | PlatformIO reports `Authentication Failed`. Bridge keeps running |
| **GitHub Release workflow** | `git tag v0.2.0 && git push origin v0.2.0` | Actions run picks up the tag, builds both envs, creates a Release with `somfyrts2mqtt-0.2.0-esp32-c3-mini.bin` + `somfyrts2mqtt-0.2.0-esp32-wroom.bin`. Visible at `/releases/latest` |
| **End-to-end (user perspective)** | From a fresh laptop : go to `/releases/latest` → download the C3 binary → open `http://somfyrts2mqtt.local/` → Update → upload → bridge restarts on the new version | No USB cable was plugged in. The update completed entirely over WiFi |

## Risks

- **Bricking via a bad upload.** The dual-app partition layout protects against truncated / corrupted binaries — the bridge keeps booting from the previous partition until `Update.end()` succeeds. But a *valid* binary that crashes on boot would still brick. Mitigation : test the binary locally (USB) before tagging a release, or use a "test in dev env first" rule. ESP32 has a `rollback_app` feature (boots back to the previous slot after N failed restarts) that we can opt into in a follow-up iter if needed.
- **OTA security.** WebOTA inherits the LAN-only attack model of the rest of the admin UI ; same threat profile. ArduinoOTA requires a password. Both rely on the user trusting their LAN. Not adding code signing for now ; would be a separate iter.
- **`firmware.bin` size growing past 1.875 MB.** Current size is ~1.1 MB so plenty of headroom, but worth tracking. CI can grep `Flash:` from `pio run` output and fail if past a threshold ; defer to a future iter.
- **Tag ↔ FW_VERSION drift.** Easy to tag `v0.2.0` while the source still says `0.1.0`. A `scripts/release.sh` that bumps `FW_VERSION` + commits + tags atomically is a useful follow-up.

## Order of work + commits

Suggested commits (kept small, reviewable independently) :

1. `feat(ota): ArduinoOTA dev workflow + mDNS service`
2. `feat(ota): WebOTA upload endpoint`
3. `feat(ui): firmware upload section with progress bar`
4. `ci: build + publish firmware binaries on tag push`
5. `docs: WebOTA + GitHub Releases instructions in setup.md, web-ui.md, README`

Each commit builds clean, builds on both envs, native tests untouched.
