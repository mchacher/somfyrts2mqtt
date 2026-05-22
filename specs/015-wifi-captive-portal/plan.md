# Plan 015

## Order of operations (as shipped)

Pure logic first, then NVS, then the `wifi_manager` rewrite, then the admin UI form. Build green at every step.

1. **Boot counter state machine** — `include/wifi_boot.h` + `test/test_wifi_boot/`. Pure inline functions, native tests.
2. **NVS extensions** — `wifi.pass` (string), `wifi.boot` (uint8), plus `tzapu/WiFiManager@^2.0.17` to `lib_deps`.
3. **`wifi_manager` rewrite** — boot decision tree, NVS-driven credentials, tzapu captive portal in `start_ap()`. Sticky-bad-BSSID + fast-path BSSID pinning preserved.
4. **Admin UI WiFi form** — section between MQTT and Remotes ; `GET/POST /api/wifi`.

Each step shipped as its own commit. CI runs build + native tests on every push.

## Step detail

### Step 1 — `include/wifi_boot.h` + `test/test_wifi_boot/`

- 3 pure inline functions :
  - `on_boot(stored, threshold)` — saturating increment (clamps at threshold to survive a stale counter from a crash mid-write).
  - `should_force_ap(post_inc, threshold)` — threshold check on the post-increment value.
  - `should_reset(stored, uptime, stable_ms)` — non-zero gate AND uptime past stable_ms.
- 11 test cases covering increment from 0..3, saturation at 4 and beyond, configurable threshold, both gates of `should_reset`.

### Step 2 — NVS extensions

- New keys pre-created in `nvs_store::init()` : `wifi.pass = ""`, `wifi.boot = 0`.
- New API : `get_wifi_ssid()`, `get_wifi_pass()`, `set_wifi_creds(ssid, pass)`, `get_boot_count()`, `set_boot_count(n)`.
- `set_wifi_creds()` also clears `wifi.bssid` / `wifi.channel` so the next boot does a fresh scan.
- `tzapu/WiFiManager@^2.0.17` added to `lib_deps` (~115 KB Flash).

### Step 3 — `wifi_manager.cpp` rewrite

- `wifi::init()` :
  - Hardening (`persistent(false)`, hostname, etc.) unchanged.
  - Boot counter : `wifi_boot::on_boot()`, persist, decide `force_ap`.
  - Compile-time creds migration (one-shot) for dev workflow.
  - If `force_ap || nvs_ssid.empty()` → reset counter → `start_ap(chip_suffix)` (BLOCKS).
  - Else STA path : existing scan + BSSID pinning, reading creds from NVS instead of compile-time constants.
- `wifi::start_ap()` :
  - Construct `WiFiManager`, `setBreakAfterConfig(true)`, timeout, debug off.
  - `wm.startConfigPortal(ap_ssid)` BLOCKS.
  - On return : persist saved creds via `set_wifi_creds()`, delay 500 ms (log flush), `ESP.restart()`.
- `wifi::loop()` :
  - Boot counter reset after stable uptime (via `wifi_boot::should_reset()`).
  - Existing sticky-bad-BSSID recovery (uses `get_wifi_ssid()/get_wifi_pass()` instead of `WIFI_SSID/WIFI_PASSWORD`).
- `on_event` GOT_IP handler tags `WifiHint` with `nvs_store::get_wifi_ssid()` instead of the constant.

### Step 4 — Admin UI WiFi form

- HTML : `<section>` between MQTT and Remotes with `kv` block (connected SSID + RSSI) and `wifi-form` (SSID + password + button).
- JS : `loadWifi()` populates the kv + pre-fills the form ; submit handler asks for confirmation, POSTs, shows "rebooting…".
- C++ : `handle_get_wifi` returns `{ssid: WiFi.SSID(), rssi: WiFi.RSSI()}`. `handle_post_wifi` validates, calls `set_wifi_creds()`, `req->send(204)`, defers 1 s, `ESP.restart()`. Empty pass + matching SSID = "keep existing" ; empty pass + new SSID = 400.

## Hardware validation (manual, post-merge)

The test plan from `spec.md` acceptance criteria :
- Fresh-flashed box (`pio run -t erase`) → boots into AP captive portal.
- Save creds in portal → reboots → STA associates.
- Configured box, SSID down → retries STA forever, never AP.
- 4 quick power-cycles → 4th boot lands in AP.
- Admin UI form → save new SSID → reboot → STA on new network.
- AP mode left idle 5 min → auto-reboot back to STA.

## Risk register

- **Captive portal HTML size** : delegated to tzapu (no inline HTML on our side). Flash budget headroom : 63.9% used on esp32-wroom, plenty.
- **DNSServer + AsyncWebServer cohabitation** : handled by tzapu (its DNS server runs only during the portal, no port-80 conflict in our AP mode).
- **`set_wifi_creds()` partial write** : 4 sequential `Preferences::putString/remove` calls. A crash between writes could leave a stale BSSID hint. The retry loop on next boot recovers ; worst case the user does 4 power-cycles to re-enter the portal.
- **WIFI_AP mode on ESP32-C3** : single radio, can't STA and AP at once. Mitigated by hard dispatch at boot — we never try to do both.
- **`clear_wifi_hint()` wipes `wifi.ssid` too** : known caveat (see architecture.md). Lives because the sticky-bad-BSSID recovery immediately re-pins. Cleanup deferred to a follow-up iter.

## Out of plan (deferred)

- A "Reset WiFi only" endpoint in the admin UI : redundant with the WiFi form (in-LAN reconfiguration) and the 4-power-cycles recovery (out-of-LAN recovery).
- Multiple stored networks (Tasmota's AP1 / AP2).
- LED indicator for AP mode (no LED wired on either board).
- `WifiHint` ssid-field cleanup (see caveat above).
