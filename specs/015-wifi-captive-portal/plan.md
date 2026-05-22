# Plan 015

## Order of operations

Pure logic first, then NVS, then runtime modules, then UI. The build stays green at every step.

1. **Boot counter state machine** (`include/wifi_boot.h`) + native tests. No deps. Locks down the increment / reset rules.
2. **NVS extensions** : `wifi.pass`, `wifi.boot_count`, `set_wifi_creds()`. Native tests for the round-trip.
3. **`wifi_manager` rewrite** : NVS-driven creds, decision tree, counter wiring, `is_ap_mode()`, `tick()`.
4. **Captive portal module** : DNSServer, HTML, `/scan.json`, `/save`, AP timeout.
5. **`main.cpp` wiring** : hard dispatch between captive portal and admin UI.
6. **Admin UI WiFi form** : section + `GET /api/wifi` + `POST /api/wifi`.
7. **`config.h` cleanup** : empty defaults for `WIFI_SSID` / `WIFI_PASSWORD`, new constants.
8. **HW validation** following the spec's acceptance criteria.
9. **PR**.

## Steps in detail

1. `include/wifi_boot.h` : write `on_boot`, `should_force_ap`, `should_reset` as inline functions. No Arduino, no NVS. 30-40 lines.

2. `test/test_wifi_boot/test_main.cpp` : 7-8 cases :
   - 0 → 1, no trigger.
   - 3 → 4 with threshold 4, trigger.
   - 4 → 5 (already in AP from a previous trigger), `>= threshold` keeps it firing — but the wifi_manager code resets to 0 before AP start so this is defensive.
   - Reset only when both `count > 0` and `uptime >= stable_ms`.
   - Reset returns false at `uptime = stable_ms - 1`.
   - Reset returns true at `uptime = stable_ms`.

3. `include/nvs_store.h` : declare `get_boot_count() / set_boot_count(n)`, `get_wifi_pass() / set_wifi_pass(p)`, `set_wifi_creds(ssid, pass)`. Doc the atomic write semantics.

4. `src/nvs_store.cpp` :
   - Pre-create `wifi.pass` (empty) and `wifi.boot_count` (0) in `init()`.
   - Implement the getters / setters.
   - `set_wifi_creds()` writes 4 keys (`wifi.ssid`, `wifi.pass`, `""` for `wifi.bssid`, `0` for `wifi.channel`) in one shot.

5. `test/test_nvs/test_main.cpp` : add cases for the new fields' default values and round-trip, and for `set_wifi_creds()` clearing the BSSID hint.

6. `include/wifi_manager.h` :
   - Drop the implicit-WIFI_SSID dependency from the public API.
   - Add `bool is_ap_mode()`, `const char* ap_ssid()`.
   - Doc that `init(now_ms)` takes the boot decision; `tick(now_ms)` drives the counter reset.

7. `src/wifi_manager.cpp` :
   - `init(now_ms)` implements the decision tree from `architecture.md`. Uses `wifi_boot::on_boot()` for the increment, `wifi_boot::should_force_ap()` for the trigger.
   - On the STA branch : `WiFi.begin(ssid.c_str(), pass.c_str(), channel, bssid)` where the channel + bssid come from NVS hints (existing fast-reconnect code stays as is). On disconnect, retry indefinitely (existing loop).
   - On the AP branch : `start_ap()` switches mode, calls `WiFi.softAPConfig()` / `softAP()` with the chip-id SSID, sets `s_ap_mode = true` and `s_ap_started_ms`.
   - `tick(now_ms)` : if `wifi_boot::should_reset()` and the counter is non-zero in our in-RAM cache, write 0 to NVS and clear the cache.

8. `include/captive_portal.h` + `src/captive_portal.cpp` :
   - `start(AsyncWebServer&)` : register routes, `dnsServer.start(53, "*", IPAddress(192,168,4,1))`.
   - Static raw-string HTML `INDEX_HTML` ≈ 3 KB.
   - `tick(now_ms)` : `dnsServer.processNextRequest()` + AP-timeout check → `ESP.restart()`.
   - `POST /save` handler uses `AsyncCallbackJsonWebHandler` (already a dep via web_ui).
   - `GET /scan.json` : sync scan with 10 s cache. Cache is a static vector ; recomputed on the first request after 10 s.

9. `src/main.cpp` :
   - Move `web_ui::init` into the STA branch.
   - Add `captive_portal::start` / `captive_portal::tick` in the AP branch.
   - The `AsyncWebServer` is created once outside the branch (so the same instance is reused if we ever want to switch modes without reboot — not in v1 but cheap insurance).

10. `src/web_ui.cpp` :
    - Add the WiFi section to the HTML between the MQTT and Remotes sections.
    - Register `GET /api/wifi` returning `{"ssid": WiFi.SSID().c_str(), "rssi": WiFi.RSSI()}`.
    - Register `POST /api/wifi` (AsyncCallbackJsonWebHandler). Validate `ssid` non-empty + ≤ 32 chars, `pass` ≤ 64 chars. If `pass` is empty but `ssid` matches the stored one, keep the existing `wifi.pass`. Otherwise refuse with 400. Call `nvs_store::set_wifi_creds()`, respond 204, defer `ESP.restart()` 1 s.
    - Add the JS loader `loadWifi()` similar to `loadMqtt()`, attach the form's submit handler.

11. `include/config.h` :
    - `WIFI_SSID` and `WIFI_PASSWORD` → `""` defaults.
    - Add `WIFI_AP_SSID_PREFIX "somfyrts2mqtt-"`, `WIFI_BOOT_STABLE_MS 5000`, `WIFI_BOOT_AP_THRESHOLD 4`, `WIFI_AP_TIMEOUT_MS (5UL * 60UL * 1000UL)`.

12. Build + check :
    ```bash
    ~/.platformio/penv/bin/pio run -e esp32-c3-mini
    ~/.platformio/penv/bin/pio run -e esp32-wroom
    ~/.platformio/penv/bin/pio test -e native
    ~/.platformio/penv/bin/pio check
    ```

13. HW validation following the spec's acceptance criteria. Erase NVS first :
    ```bash
    ~/.platformio/penv/bin/pio run -e esp32-c3-mini -t erase
    ~/.platformio/penv/bin/pio run -e esp32-c3-mini -t upload
    ```

14. Commit per logical step (boot counter + tests, NVS extensions, wifi_manager rewrite, captive portal module, main wiring, admin UI). Open PR with the standard checklist.

## Risk register

- **Captive portal HTML size** : ESP32 AsyncWebServer can serve big raw strings but the device has only ~250 KB RAM. The portal HTML stays inline ≤ 4 KB.
- **DNSServer + AsyncWebServer cohabitation** : verified working in countless ESPHome / Tasmota deployments. No known conflict at port 53 / 80.
- **`set_wifi_creds()` partial write** : `Preferences::putString` is synchronous and atomic per key, but the 4 keys are written sequentially. A crash between writes could leave a stale BSSID hint pointing at the old router. Acceptable : the next STA attempt fails fast (timeout) and the retry loop / 4-power-cycle covers recovery.
- **WIFI_AP mode on ESP32-C3** : single radio, can't STA and AP at once. We don't try — hard dispatch at boot.

## Out of plan (deferred)

- A "Reset WiFi only" endpoint in the admin UI (wipe `wifi.*` keys, reboot to AP) : the WiFi form covers the "I want to change my SSID" case ; the 4-power-cycle covers the "I lost LAN access" case. A dedicated reset endpoint would be redundant.
- Multi-network (AP1/AP2 like Tasmota).
- LED indicator for AP mode (no LED wired on either board).
