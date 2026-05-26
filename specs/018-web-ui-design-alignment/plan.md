# Plan 018 — Captive portal aligned on sowel-energy-display

## Steps

1. **Add `include/captive_portal.h`** with the `[[noreturn]] void run(const char* ap_ssid, uint32_t timeout_s)` declaration.

2. **Add `src/captive_portal.cpp`** with:
   - `STYLE[]` PROGMEM constant copied verbatim from
     `sowel-energy-display/src/portal.cpp`.
   - `html_escape()` helper (copy from energy-display).
   - `scan_networks()` that returns an `<option>` list sorted by RSSI desc.
   - `render_form(last_error)` rendering the SOWEL header + WiFi card.
   - `render_saved_page()` rendering the "Configuration enregistrée,
     redémarrage…" confirmation.
   - `on_save(req)` extracting `wifi_ssid` + `wifi_pass`, validating, calling
     `nvs_store::set_wifi_creds()`, flipping `pending_reboot`.
   - `run()` orchestrating AP setup + DNSServer + AsyncWebServer + the
     `while` loop with idle timeout + final `ESP.restart()`.

3. **Edit `src/wifi_manager.cpp`**:
   - Remove `#include <WiFiManager.h>`.
   - Replace the body of `start_ap()` with a single call to
     `captive_portal::run(ap_ssid, WIFI_AP_TIMEOUT_S)`.
   - Keep the `chip_suffix` -> `ap_ssid` formatting unchanged.

4. **Edit `platformio.ini`**: remove the `tzapu/WiFiManager` line from
   `lib_deps`. Confirm `me-no-dev/ESPAsyncWebServer` and `DNSServer` (core)
   are still listed where needed.

5. **Build**: `~/.platformio/penv/bin/pio run -d .`
   - Zero warnings, zero errors on both envs (`esp32-c3-mini`, `esp32-wroom`).

6. **Static check**: `~/.platformio/penv/bin/pio check -d .` — clean.

7. **Native tests**: `~/.platformio/penv/bin/pio test -d . -e native` —
   no captive-portal-specific tests (HW-bound), but the existing suites
   (NVS, parsers, etc.) must still pass.

8. **Flash to bench device**: `~/.platformio/penv/bin/pio run -d . -t upload`.

9. **Run HW test plan below.**

## Test plan (HW)

Prerequisite: wipe NVS on the bench device so the bridge boots into AP
mode. Either run a factory reset from the LAN page, or flash with `pio run
-t erase` + upload.

### Captive flow

- [ ] Serial log shows `[wifi] captive portal SSID=somfyrts2mqtt-XXXX
      timeout=300s` (or whatever `WIFI_AP_TIMEOUT_S` is set to).
- [ ] A 2.4 GHz network named `somfyrts2mqtt-XXXX` is visible from a phone.
- [ ] Joining the AP triggers the iOS "Sign in to Wi-Fi network" /
      Android "Sign in to network" captive-portal sheet automatically
      (DNS catch-all + standard probe responses).
- [ ] If the captive sheet doesn't auto-open, manually browsing to
      `http://192.168.4.1/` still loads the page (fallback).

### Visual

- [ ] Page background is the light Sowel `#F8F9FA` grey, NOT tzapu's
      generic dark grey.
- [ ] "SOWEL" brand displayed at top (letter-spacing 4px, color
      `#1A4F6E`), with tagline "Configuration du bridge Somfy RTS" below.
- [ ] WiFi card has a white background, rounded corners, Sowel-primary
      uppercase h2.
- [ ] SSID select shows the list of nearby networks sorted by signal
      strength.
- [ ] Password field has the eye-toggle button on the right; clicking
      it switches type=password ↔ type=text.
- [ ] Submit button is a full-width amber Sowel-primary button labelled
      "Valider et redémarrer".
- [ ] Page renders OK with no Google Fonts (system stack fallback —
      Apple system / Inter / Segoe UI / Roboto / sans-serif).

### Submit flow

- [ ] Submitting with a valid SSID + password:
      - returns a "Configuration enregistrée…" page,
      - serial log shows `[wifi] saved new creds ssid=<…>, rebooting into STA`,
      - device reboots within 1 s,
      - boots into STA, joins the saved network successfully.
- [ ] Submitting with an empty SSID: page re-renders with a red error
      banner "SSID requis" above the form (no NVS write, no reboot).
- [ ] Submitting an open network (empty password) is accepted and works.

### Timeout flow

- [ ] Leave the captive open without submitting for the full timeout.
      Serial logs `[wifi] captive portal timed out after Xs without
      save, rebooting`. Bridge reboots and re-enters AP mode if NVS
      is still empty.

### PA saturation regression check (C3 Super Mini)

- [ ] On a Super Mini board, the AP is joinable from a phone (no
      `WL_NO_SSID_AVAIL` or auth failures). TX power should be clamped
      to `WIFI_POWER_8_5dBm` while in AP mode.

### Build size sanity

- [ ] `pio run -e esp32-c3-mini` output shows a SMALLER firmware after
      dropping tzapu (typical delta -40 to -80 KB). Logged for the PR
      body.

## Edge cases

- [ ] Phone disconnects mid-submit → the user re-joins the AP, hits the
      page again, can re-submit. No stale state.
- [ ] Bridge has 0 visible networks during scan → friendly "Aucun réseau
      visible" message with a Re-scan link.
- [ ] User enters a 64-char password (boundary): `maxlength="63"`
      enforces, server-side also caps at 63 (the WPA2-PSK max).
- [ ] SSID contains characters that would break HTML (`<`, `>`, `&`,
      quotes) → all rendered through `html_escape()`.
