# Architecture 018 — Captive portal aligned on sowel-energy-display

## Touched modules

| File                          | Change                                                                              |
| ----------------------------- | ----------------------------------------------------------------------------------- |
| `include/captive_portal.h`    | NEW — public API `captive_portal::run(ap_ssid, timeout_s)`                          |
| `src/captive_portal.cpp`      | NEW — AP + DNSServer + AsyncWebServer + inlined HTML/CSS                            |
| `src/wifi_manager.cpp`        | `start_ap()` now calls `captive_portal::run(...)`. Removes `<WiFiManager.h>` usage. |
| `platformio.ini`              | Drop `tzapu/WiFiManager` from `lib_deps`. Keep DNSServer + ESPAsyncWebServer.       |
| `include/config.h`            | If needed, expose `WIFI_AP_TIMEOUT_S` and `WIFI_AP_SSID_PREFIX` (already there).    |

## Decisions

**Selected option: copy the energy-display portal architecture verbatim,
adapt the form to bridge-specific fields only.**

The energy-display portal is small, well-isolated, and battle-tested. It is
also already aligned with Sowel's design language. Copying its shape gives
us instant consistency without reinventing AP/DNS/HTTP plumbing.

**Rejected alternatives:**

1. *Keep tzapu and try to inject custom CSS via
   `wm.setCustomHeadElement()` / `wm.setCustomMenuHTML()`.* — Partial
   customization at best (tzapu's library still injects its own header
   markup, table structure, button shapes); the result would never look
   like a real sibling of the energy-display. The user explicitly asked
   for full alignment.
2. *Reuse `web_ui.cpp` for both LAN and AP* — They have different
   constraints (AP = offline, no Google Fonts; LAN = online, can pull
   anything) and different scopes (AP = WiFi creds only, LAN = MQTT +
   remotes + WebOTA). Better to keep them separate. The LAN page may
   later be brought to the same light palette in a follow-up iter.

## Library impact

- **Removed**: `tzapu/WiFiManager` (~80 KB flash, transitive deps on
  ESPAsyncWebServer + DNSServer which we re-use directly).
- **Kept (already in `lib_deps`)**:
  - `me-no-dev/ESP Async WebServer` — used both by the existing
    `web_ui.cpp` and the new captive portal.
  - `DNSServer` — bundled with the ESP32 Arduino core.

Net effect: smaller binary, one less third-party library to track.

## API of the new module

```cpp
// include/captive_portal.h
#pragma once
#include <cstdint>

namespace captive_portal {

  /// Start the AP + DNS + HTTP captive portal and BLOCK until the user
  /// submits WiFi creds (returns true) or `timeout_s` elapses (returns
  /// false). Persists the entered SSID + password to NVS on submit.
  /// Always ends by calling `ESP.restart()` so the caller never sees
  /// the function return.
  ///
  /// Behavior mirrors `wifi_manager::start_ap()` today but with our own
  /// HTML/CSS instead of tzapu's.
  ///
  /// @param ap_ssid    null-terminated AP SSID (e.g. "somfyrts2mqtt-A1B2")
  /// @param timeout_s  idle timeout in seconds (no form submit -> reboot)
  [[noreturn]] void run(const char* ap_ssid, uint32_t timeout_s);

}
```

## Flow

```
wifi_manager::start_ap(chip_suffix)
  └── builds "somfyrts2mqtt-XXXX"
  └── captive_portal::run(ap_ssid, WIFI_AP_TIMEOUT_S)
        │
        ├── WiFi.mode(AP_STA)              // STA stays up so scan works
        ├── WiFi.softAPConfig(AP_IP, AP_GW, AP_MASK)
        ├── WiFi.softAP(ap_ssid, nullptr, AP_CHAN, AP_HIDDEN, AP_MAXCO)
        ├── WiFi.setTxPower(WIFI_POWER_8_5dBm)   // PA saturation clamp
        ├── DNSServer.start(53, "*", AP_IP)      // catch-all DNS
        ├── AsyncWebServer http(80);
        │     ├── GET  /         → render_form()
        │     ├── POST /save     → on_save()
        │     ├── GET  /generate_204  → render_form() // Android probe
        │     ├── GET  /hotspot-detect.html → render_form() // iOS probe
        │     ├── GET  /ncsi.txt → render_form() // Windows probe
        │     └── GET  *         → 302 to /
        ├── http.begin()
        │
        ├── while (millis() - start < timeout_s*1000 && !pending_reboot)
        │     ├── dns_server.processNextRequest()
        │     └── delay(10)
        │
        └── ESP.restart()
              (after a 500 ms log flush)

  on_save(req):
    ssid = req.param("wifi_ssid")
    pass = req.param("wifi_pass")
    if (ssid.empty()) → render_form_with_error("SSID requis")
    nvs_store::set_wifi_creds(ssid, pass)
    pending_reboot = true                  // picked up by the loop above
    respond_html("<html>…Configuration enregistrée, redémarrage…</html>")
```

## HTML/CSS

Single `INDEX_HTML` template assembled in `render_form()` from a `PROGMEM`
`STYLE[]` constant + form scaffold. The STYLE constant is copied **verbatim**
from `sowel-energy-display/src/portal.cpp` (palette tokens, body, container,
header.brand, .card, .card h2, .field, .field label, .field input,
.field select, .row, .btn, .with-toggle, .toggle-pass, .error, .footer).

DOM structure:

```html
<div class="container">
  <header>
    <div class="brand">SOWEL</div>
    <div class="tag">Configuration du bridge Somfy RTS</div>
  </header>

  <!-- optional error banner -->
  <div class="error">…</div>

  <form method="POST" action="/save">
    <div class="card">
      <h2>WiFi</h2>
      <div class="field">
        <label for="wifi_ssid">Réseau</label>
        <select name="wifi_ssid" required>…scan…</select>
        <span class="hint">Choisis ton réseau 2.4 GHz.</span>
      </div>
      <div class="field">
        <label for="wifi_pass">Mot de passe</label>
        <div class="with-toggle">
          <input type="password" name="wifi_pass" maxlength="63" autocomplete="new-password">
          <button type="button" class="toggle-pass">…SVG eye…</button>
        </div>
        <span class="hint">Laisser vide pour un réseau ouvert.</span>
      </div>
    </div>

    <button class="btn" type="submit">Valider et redémarrer</button>
  </form>

  <div class="footer">somfyrts2mqtt</div>
</div>
```

## State management

`pending_reboot` is a `volatile bool` flipped from the HTTP callback (which
runs on the AsyncTCP task) and observed from the main task running the
`while` loop. ESP.restart() is called from the main task — never from the
HTTP callback (mirrors the explicit warning in the energy-display portal).

`last_error` (static `String`) is rendered back into the page on the next
GET after a validation failure. Cleared on the next successful flow.

## Edge cases handled

- **Empty scan result** — render the `<select>` with one disabled
  `<option>` saying "Aucun réseau visible — actualiser dans 5 s", plus a
  small "Re-scan" link that refreshes the page. (Mirrors what the
  energy-display does when boot scan returns empty.)
- **SSID with double quotes / `<` / `>`** — pass through `html_escape()`
  helper before injection.
- **Two clients simultaneously hitting POST /save** — single
  `pending_reboot` flag, second request gets a 503 "Already pending"
  response. Not a real-world concern but trivial to handle.
