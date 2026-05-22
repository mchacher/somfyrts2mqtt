# Architecture 015

## Touched modules

| File | Change |
|---|---|
| `include/config.h` | Drops legacy `SETUP_AP_SSID`. New constants : `WIFI_AP_SSID_PREFIX`, `WIFI_BOOT_STABLE_MS`, `WIFI_BOOT_AP_THRESHOLD`, `WIFI_AP_TIMEOUT_S`. |
| `include/secrets.h.example` | `WIFI_SSID` / `WIFI_PASSWORD` default to `""`. A fresh checkout boots into AP captive portal ; non-empty values still work as a one-shot migration source on first boot. |
| `include/nvs_store.h`, `src/nvs_store.cpp` | New keys `wifi.pass` (string) and `wifi.boot` (uint8). New API : `get_wifi_ssid()`, `get_wifi_pass()`, `set_wifi_creds(ssid, pass)`, `get_boot_count()`, `set_boot_count(n)`. `set_wifi_creds()` also clears the BSSID hint atomically. |
| `include/wifi_boot.h` | New pure-logic header for the boot counter state machine (saturating increment + stable-uptime reset). Testable on native. |
| `include/wifi_manager.h`, `src/wifi_manager.cpp` | Rewrite. Boot decision tree at the top of `init()`, NVS-driven credentials (instead of the compile-time `WIFI_SSID` / `WIFI_PASSWORD`), tzapu/WiFiManager captive portal in `start_ap()`. Existing sticky-bad-BSSID recovery + WifiHint fast-path preserved. |
| `src/web_ui.cpp` | New "WiFi" section in the admin UI between MQTT and Remotes. `GET /api/wifi` returns the live SSID + RSSI ; `POST /api/wifi` writes credentials via `set_wifi_creds()` and `ESP.restart()`s after a 1 s defer. |
| `test/test_wifi_boot/test_main.cpp` | 11 native tests for the boot counter pure logic. |
| `platformio.ini` | Adds `tzapu/WiFiManager@^2.0.17` to `lib_deps`. |

Files unchanged : `rf.*`, `mqtt.*`, `orchestrator.*`, `main.cpp`. The blocking nature of AP mode means the main `setup()` ordering takes care of skipping MQTT / orchestrator init for free.

## NVS layout extensions

Existing keys (no change) : `wifi.ssid`, `wifi.bssid`, `wifi.channel`.

New keys :

```
wifi.pass   std::string   default ""   the WiFi password (paired with wifi.ssid)
wifi.boot   uint8_t       default 0    boot counter for 4-power-cycles AP recovery
```

`set_wifi_creds(ssid, pass)` writes `wifi.ssid` + `wifi.pass`, then `s_prefs.remove("wifi.bssid")` + `s_prefs.remove("wifi.channel")` so the next boot does a fresh scan against the new router instead of trying to associate with the old BSSID.

Note : `clear_wifi_hint()` (existing API, used by the sticky-bad-BSSID recovery) still removes all three of `wifi.ssid` / `wifi.bssid` / `wifi.channel`. In the new design, `wifi.ssid` is the credential SSID, so clearing it would lose the creds. This is a known caveat handled by the recovery flow itself : the rescan immediately re-pin a new hint with the SSID we know from the `nvs_store::get_wifi_ssid()` call that preceded the rescan. Cleaner refactor (drop `ssid` from WifiHint, never wipe creds) is deferred to a follow-up iter to keep this PR scoped.

## Boot decision tree

`wifi::init()` runs once at startup, before any network activity :

```
                ┌──────────────────────────────────┐
                │ count = nvs.get_boot_count()     │
                │ new   = wifi_boot::on_boot(...)  │
                │ nvs.set_boot_count(new)          │
                └──────────────────────────────────┘
                                │
                ┌───────────────┴───────────────┐
                │ wifi_boot::should_force_ap()? │
                └───────────────┬───────────────┘
                yes │                          │ no
                    ▼                          ▼
        ┌──────────────────────┐  ┌────────────────────────┐
        │ nvs.set_boot_count(0)│  │ ssid = nvs.get_ssid()  │
        │ start_ap()  [BLOCKS] │  │ // migrate from        │
        │ ESP.restart()        │  │ //   secrets.h if NVS  │
        └──────────────────────┘  │ //   empty and SSID    │
                                  │ //   compile-time set  │
                                  └────────────┬───────────┘
                                ┌──────────────┴──────────────┐
                                │ ssid empty ?                │
                                └──────────────┬──────────────┘
                                yes │                       │ no
                                    ▼                       ▼
                          ┌─────────────────┐  ┌──────────────────────┐
                          │ start_ap()      │  │ WiFi.begin(ssid,pass)│
                          │   [BLOCKS]      │  │ + BSSID hint if any  │
                          │ ESP.restart()   │  │ retry forever on fail│
                          └─────────────────┘  └──────────────────────┘
```

Saturating increment (`wifi_boot::on_boot`) clamps the post-increment value at `WIFI_BOOT_AP_THRESHOLD` so a stale counter (e.g. crash mid-write) still triggers AP rather than wrapping to 0.

The counter is reset to 0 in two cases :
- Threshold reached → AP started → counter reset (so a single reboot out of AP is a clean STA boot).
- After `WIFI_BOOT_STABLE_MS` ms of uptime → counter reset (the "you booted normally" signal). Driven by `wifi::loop()` calling `wifi_boot::should_reset()` and writing 0 once.

## AP mode via tzapu/WiFiManager

`wifi::start_ap(chip_suffix)` is the only entry point. It constructs a transient `WiFiManager` instance, configures it, calls `startConfigPortal()` (BLOCKING), and `ESP.restart()`s on return.

```cpp
WiFiManager wm;
wm.setBreakAfterConfig(true);             // return after save; do not let
                                          // tzapu WiFi.begin() itself
wm.setConfigPortalTimeout(WIFI_AP_TIMEOUT_S);
wm.setDebugOutput(false);                 // tzapu's debug stream is verbose

const bool saved = wm.startConfigPortal(ap_ssid);
if (saved) {
  nvs_store::set_wifi_creds(wm.getWiFiSSID().c_str(),
                             wm.getWiFiPass().c_str());
}
ESP.restart();
```

What tzapu handles for us :
- SoftAP setup (`WiFi.mode(WIFI_AP_STA)`, `WiFi.softAP(ap_ssid)`).
- `DNSServer` on port 53 catching every query (captive portal detection).
- Portal HTML : home page, WiFi scan + select, credential form.
- Captive-portal probe URLs (`/generate_204`, `/hotspot-detect.html`, ...) redirected to the portal.
- AP timeout (auto-exit on `setConfigPortalTimeout` expiry).

What we keep on our side :
- Boot decision (counter + creds check) before tzapu sees anything.
- Credential storage (in our NVS namespace, alongside MQTT + remotes).
- BSSID pinning for fast reconnect (tzapu only handles the portal phase).
- `WiFi.persistent(false)` to avoid flash wear from auto-saved creds.

`setBreakAfterConfig(true)` is the critical knob : without it, tzapu would call `WiFi.begin()` itself after save and never return. With it, the portal returns control to us as soon as the user clicks Save, so we can persist to our NVS and trigger a clean reboot into the normal STA path.

## Admin UI WiFi form (STA mode)

`GET /api/wifi` returns the live state :

```json
{"ssid": "GrangeNeuve_Freebox_2.4Ghz", "rssi": -56}
```

`POST /api/wifi` with body `{"ssid": "...", "pass": "..."}` :
- `ssid` non-empty, length ≤ 32, required.
- `pass` length ≤ 64. Empty value :
  - if `ssid` matches the stored one → keep the existing password (rotate something else).
  - else → 400 "password required for new SSID".
- On success : `nvs_store::set_wifi_creds()`, 204, defer 1 s, `ESP.restart()`.

The form pre-fills the SSID with the currently connected one (most common case : same network, password change). The submit handler asks for explicit confirmation and reminds the user of the 4-power-cycles recovery in case the new creds are wrong.

## Module lifecycle in `main.cpp`

No structural change : the existing `setup()` is sequential and the AP branch in `wifi::init()` is blocking + reboots, so MQTT / orchestrator / admin UI never run in AP mode.

```cpp
void setup() {
  // ...
  nvs_store::init();
  bootstrap_mqtt_from_secrets();
  rf::init();
  wifi::init();                  // BLOCKS in AP mode; returns immediately in STA
  mqtt::init();                  // STA path only
  orchestrator::init_runtimes(); // STA path only
  // ... grace window waiting for IP ...
  web_ui::init();                // STA path only
}
```

## Failure modes and edge cases

- **NVS write fails during counter increment** : `nvs_store::set_boot_count` returns false. Logged ; treated as if the counter is 0 (graceful degradation, recovery just won't trigger this boot).
- **User saves an SSID that doesn't exist / wrong password** : next boot, STA fails, retries forever. 4 quick power-cycles to recover.
- **Counter overflow** : saturating increment prevents wrap-around. uint8 max stays at threshold.
- **Stale BSSID hint after creds change** : `set_wifi_creds()` clears it so the next boot does a fresh scan.
- **AP mode triggered by accident** : `WIFI_AP_TIMEOUT_S` (5 min) expires without a save → `ESP.restart()` → STA retry with the existing NVS creds.
- **tzapu's `WiFi.persistent(true)` side effect** : tzapu may flip the persistence flag internally. Our `init()` sets `persistent(false)` BEFORE calling tzapu, then the AP branch reboots, so the STA path always restarts with a fresh `persistent(false)` state. No flash-wear regression.
