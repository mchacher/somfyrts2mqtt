# 018 — Captive portal aligned on sowel-energy-display

## Goal

Drop the tzapu WiFiManager captive portal and replace it with our own AP
captive portal mirroring the implementation of `sowel-energy-display`
(`src/portal.cpp`). Same brand header, same Sowel light palette, same
inlined CSS, same system fonts, same form patterns — so the first-run
experience of the somfyrts2mqtt bridge looks visibly like a sibling of the
energy-display.

Today the bridge boots into a generic tzapu portal (white-on-grey, library
default) that doesn't identify itself as Sowel and doesn't match anything
else in the product family.

## Scope

In scope:
- Remove `tzapu/WiFiManager` from `lib_deps` and from `wifi_manager.cpp`.
- New `src/captive_portal.cpp` + `include/captive_portal.h` modeled on the
  energy-display's `portal.cpp` / `portal.h`:
  - `WiFi.softAP("somfyrts2mqtt-XXXX")` + `DNSServer` (catch-all → AP IP,
    triggers iOS/Android captive redirect).
  - `ESPAsyncWebServer` on port 80, serving one inlined HTML page.
  - Inlined CSS in `PROGMEM` using the exact Sowel light palette from the
    energy-display portal (`--primary #1A4F6E`, `--primary-light #E6F0F6`,
    `--accent #D4963F`, `--bg #F8F9FA`, `--card #FFFFFF`, `--ink #1A2A3C`,
    `--muted`, `--border`, `--error`, `--error-bg`) and the same system
    font stack (no Google Fonts — the phone is offline once joined to the
    AP).
  - Same DOM scaffold (`.container > header.brand + .card[h2 + .field…] +
    .btn`) and the same eye-toggle widget for the password field.
- Form fields needed by the bridge (different from the energy-display, only
  what we actually need at first-run):
  - Wi-Fi SSID (dropdown from `WiFi.scanNetworks()`)
  - Wi-Fi password (with eye toggle)
- POST `/save` → validate → `nvs_store::set_wifi_creds()` → `ESP.restart()`,
  exactly the same control flow `wifi_manager::start_ap()` has today.
- Preserve the existing `WiFi.setTxPower(WIFI_POWER_8_5dBm)` clamp that
  `setAPCallback` was doing under tzapu — required for the C3 Super Mini
  PA-saturation workaround (see CLAUDE.md note + previous iteration).
- Preserve the existing AP timeout (`WIFI_AP_TIMEOUT_S`, 5 min today) —
  after that idle period the bridge reboots back into STA-attempt mode.

Out of scope:
- The LAN web UI `web_ui.cpp` (the page served on the local network once
  the bridge is joined to Wi-Fi). It already has its own dark-themed CSS;
  bringing it to the same light theme is a follow-up iteration.
- MQTT broker config — stays on the LAN page, not duplicated in the
  captive portal (the energy-display happens to do MQTT-equivalent
  fields in its portal because that's its only config surface; the
  bridge already has the LAN page for MQTT, so we keep them separate).
- Replacing `ArduinoOTA` (espota) — untouched.

## Acceptance criteria

- [ ] `lib_deps` in `platformio.ini` no longer references
      `tzapu/WiFiManager`. Build still passes both envs (`esp32-c3-mini`
      and `esp32-wroom`).
- [ ] Booting the bridge with empty NVS brings up an AP named
      `somfyrts2mqtt-XXXX` (last 4 of MAC), 2.4 GHz, channel 1.
- [ ] Joining that AP from a phone triggers the OS captive-portal
      notification (DNSServer catch-all working).
- [ ] The captive portal page loads on `http://192.168.4.1/` and
      visually matches the energy-display portal:
      - SOWEL brand header + tagline (e.g. "Configuration du bridge
        Somfy RTS")
      - Light bg `#F8F9FA`, white cards `#FFFFFF`, primary `#1A4F6E`
      - System sans-serif font (Apple system / Inter / Segoe / Roboto)
      - Same `.field`, `.with-toggle`, `.btn` widget styling
- [ ] Wi-Fi SSID is a dropdown populated from `WiFi.scanNetworks()`
      (sorted by RSSI desc, deduplicated).
- [ ] Password field has the inline eye toggle (same SVG icon as
      energy-display).
- [ ] Submitting the form saves SSID + password to NVS and triggers a
      reboot within 1 s. The new creds are picked up on the next boot.
- [ ] AP timeout (`WIFI_AP_TIMEOUT_S`) still triggers an automatic
      reboot if the user never submits the form.
- [ ] TX power is still clamped to `WIFI_POWER_8_5dBm` while in AP mode
      (PA saturation workaround for the C3 Super Mini).
- [ ] Build clean (`pio run`, `pio check`), all native unit tests still
      pass.
- [ ] Manual HW test passed on the bench device with an iPhone joining
      the AP.
