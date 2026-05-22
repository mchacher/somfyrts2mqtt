# 015 — WiFi captive portal (Tasmota-style commissioning)

## Goal
Remove the need to flash WiFi credentials at compile time. A fresh box boots into a SoftAP captive portal that lets the user enter SSID + password from any phone or laptop. A box already on a network can be reconfigured from the admin UI without ever going back to AP mode. If the box is physically inaccessible (admin UI unreachable, hidden behind a shutter), 4 rapid power-cycles force AP recovery — no serial console, no factory reset, no firmware re-flash.

## Background
Today `wifi_manager.cpp` calls `WiFi.begin(WIFI_SSID, WIFI_PASSWORD, ...)` with compile-time constants from `config.h`. Provisioning a new box requires editing those constants and re-flashing. `config.h:46` already names a placeholder AP SSID but no implementation exists. NVS has `wifi.ssid` / `wifi.bssid` / `wifi.channel` (used today only for the BSSID fast-reconnect hint) but no `wifi.pass`.

This iter aligns with Tasmota's actual `WifiConfig 4` (WIFI_RETRY) default behavior :
- AP mode only when there is no choice : either no creds in NVS, or the user explicitly forces recovery.
- If creds exist but the network is down, retry STA forever — the box reconnects automatically as soon as the router is back, no manual intervention needed.
- Recovery from a stale config (wrong creds, moved router, lost admin UI access) is via **4 rapid power-cycles** : a tiny boot counter in NVS that increments at every boot and resets after a few seconds of stable uptime. Reaching the threshold forces AP mode for one boot only. Power management hardware (a wall switch, a relay, the PSU socket) is enough — no GPIO button required.

For the common "I am changing my router and the box is still on the LAN" case, a WiFi form is added to the admin UI : same effect as the captive portal but reachable while STA is up.

## Scope

**In scope:**

### NVS additions
- `wifi.pass` : string, default `""`. Reuse the existing `wifi.ssid` (currently always empty in fresh installs ; no migration concern).
- `wifi.boot_count` : uint8, default 0. Incremented at every boot, reset to 0 after `WIFI_BOOT_STABLE_MS` of uptime (default 5000 ms).
- On AP-mode save, also clear `wifi.bssid` / `wifi.channel` to force a clean scan on the next boot.

### Boot decision tree (in `wifi_manager::init`)
1. Read `wifi.boot_count` ; increment ; save. Schedule a `wifi.boot_count = 0` write at `now + WIFI_BOOT_STABLE_MS`.
2. If `wifi.boot_count >= WIFI_BOOT_AP_THRESHOLD` (default 4) → enter AP mode. Reset the counter to 0 before starting AP (so a single power-cycle out of AP returns to STA).
3. Else if `wifi.ssid` is empty → enter AP mode.
4. Else `WiFi.begin(ssid, pass)` with the BSSID hint (existing fast-reconnect path). On disconnect, keep retrying STA forever (existing reconnect logic stays).

### AP mode
- SoftAP SSID `somfyrts2mqtt-<chipId6>` (chipId6 = last 6 hex of MAC, matches the MQTT `client_id` suffix). Open network — short-lived commissioning on local LAN, no password needed.
- IP `192.168.4.1`, DHCP for the connecting client.
- `DNSServer` resolves every query to `192.168.4.1` → triggers the OS captive-portal pop-up on iOS / Android / macOS / Win11.
- Captive portal HTML on the existing `AsyncWebServer` instance, with a separate route set registered only in AP mode :
  - `GET /` → page with a refreshable WiFi scan dropdown + password input + Save button.
  - `GET /scan.json` → `[{ssid, rssi, secure, channel}, ...]` from `WiFi.scanNetworks()`.
  - `POST /save` → body `{ssid, pass}`. Non-empty SSID required. Writes NVS, responds 204, then `ESP.restart()` after a 1 s defer to flush the response.
  - All other routes return 404 in AP mode. The admin UI (Status / MQTT / Remotes / Danger zone) is NOT served in AP mode.
- **AP timeout** : after `WIFI_AP_TIMEOUT_MS` (default 5 min) without a successful `POST /save`, `ESP.restart()`. Prevents the box from being stuck in AP forever if a recovery trigger fired by accident — the next boot tries STA with whatever creds are still in NVS.

### Admin UI WiFi form (STA mode only)
- New `<section><h2>WiFi</h2>...</section>` in `web_ui.cpp`, between "MQTT broker" and "Remotes".
- Fields : SSID (text), Password (password, placeholder `(unchanged)`). Current connected SSID + RSSI shown read-only above the form for context.
- Save → `POST /api/wifi` `{ssid, pass}`. Non-empty SSID required. Writes NVS, 204, deferred restart. Same NVS path as the captive portal `/save`.

### `config.h` cleanup
- `WIFI_SSID` / `WIFI_PASSWORD` stay as compile-time constants but **default to `""`**. A fresh checkout boots into AP mode immediately. If a developer wants to skip the AP dance during local testing, they can still set the constants in their local `config.h` — the firmware uses them as the first-boot defaults and writes them to NVS on the first successful STA association.

### Late additions (post-step-4)
- **mDNS responder** : `MDNS.begin(hostname)` + `MDNS.addService("http", "tcp", 80)` fired once on `STA_GOT_IP`. The bridge becomes reachable as `<hostname>.local` on any LAN with Bonjour / avahi support. No new lib_dep (ESPmDNS ships with Arduino-ESP32).
- **Short hostname** : the STA / mDNS hostname is now plain `somfyrts2mqtt` (no MAC suffix). The AP SSID keeps the suffix (`somfyrts2mqtt-<chipId6>`) so two bridges entering setup mode in the same physical space remain distinguishable. A future iter can make the hostname configurable via the web UI for multi-bridge LANs.
- **TX power 8.5 dBm** : explicit `WiFi.setTxPower(WIFI_POWER_8_5dBm)` in both the STA path (right after `WiFi.mode(WIFI_STA)`) and the AP path (via tzapu's `setAPCallback`). Bypasses the ESP32-C3 Super Mini PA saturation that triggers AUTH_EXPIRE / association issues on some boards. Harmless on WROOM. Backported from `fix/c3-wifi-tx-power` (PR #20 on main).

**Out of scope:**
- GPIO long-press button to force AP. The 4-power-cycles recovery covers the inaccessible-box case without hardware change.
- Runtime STA disconnect → AP fallback. Pure Tasmota `WifiConfig 4` behavior : retry forever. Stable network access for remote control matters more than self-healing into an unreachable AP.
- Multiple stored networks (Tasmota's AP1 / AP2). One stored SSID is enough for a fixed-location box.
- Captive portal HTTPS. AP is open and short-lived ; plain HTTP is fine.
- WPS, SmartConfig, ESP-Touch, BLE provisioning, Improv-WiFi.
- Auto-MQTT-config from the portal. MQTT broker config stays in the admin UI, set after the box is on the LAN.

## Acceptance criteria
- [ ] Build clean on `esp32-c3-mini` and `esp32-wroom`.
- [ ] `pio check` zero defects, `pio test -e native` all green (new cases for the boot-counter state machine).
- [ ] HW : fresh-flashed box (`pio run -t erase`) boots straight into AP `somfyrts2mqtt-XXXXXX`. Phone joins → captive portal pop-up shows the form.
- [ ] HW : Save valid creds → reboot → STA connects → admin UI reachable at the LAN IP. Reboot a second time → STA reconnects directly (no AP).
- [ ] HW : box configured for SSID "A", power-cycled with SSID "A" turned off → keeps retrying STA forever, never AP. Re-enable SSID "A" → STA associates within the next retry window. No manual action required.
- [ ] HW : box on STA, 4 power-cycles within 5 s each → 4th boot lands in AP mode. Configure a different SSID via the portal → boots into the new STA on next reboot.
- [ ] HW : box on STA, admin UI WiFi form → enter new SSID + pass → 204 → reboot → STA connects on the new network.
- [ ] HW : box in AP mode, no save action for 5 min → auto-reboot → retries STA.
- [ ] HW : `ping somfyrts2mqtt.local` resolves and answers once the box is in STA mode. `http://somfyrts2mqtt.local/` opens the admin UI.
- [ ] HW : C3 Super Mini boards that previously hit AUTH_EXPIRE on the Freebox connect successfully with the 8.5 dBm clamp (validated on board #2).
- [ ] CI green on the PR.
