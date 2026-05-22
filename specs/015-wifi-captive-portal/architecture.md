# Architecture 015

## Touched modules

| File | Change |
|---|---|
| `include/config.h` | `WIFI_SSID` / `WIFI_PASSWORD` default to `""`. New constants : `WIFI_AP_SSID_PREFIX`, `WIFI_BOOT_STABLE_MS`, `WIFI_BOOT_AP_THRESHOLD`, `WIFI_AP_TIMEOUT_MS`. |
| `include/nvs_store.h`, `src/nvs_store.cpp` | Add `wifi.pass` (string) and `wifi.boot_count` (uint8) keys with getters / setters. New `set_wifi_creds(ssid, pass)` writes both creds + clears `wifi.bssid` / `wifi.channel` atomically. |
| `include/wifi_boot.h` | New pure-logic header for the boot-counter state machine. Testable on native. |
| `include/wifi_manager.h`, `src/wifi_manager.cpp` | Major rewrite. Decision tree at boot, read creds from NVS instead of compile-time constants, expose `is_ap_mode()`, drive the boot counter, schedule the post-stable counter reset. |
| `include/captive_portal.h`, `src/captive_portal.cpp` | New module. `DNSServer` + captive HTML + `GET /scan.json` + `POST /save`. Started by `wifi_manager` when entering AP mode. Owns the AP-mode `AsyncWebServer` route set. |
| `src/web_ui.cpp` | New WiFi section in the admin UI + `POST /api/wifi` handler. Skipped entirely in AP mode (the captive portal owns the server then). |
| `src/main.cpp` | Call `wifi_manager::tick(now_ms)` from `loop()` (drives the stable-uptime counter reset and the AP-timeout reboot). Add a DNS server tick when in AP mode. |
| `test/test_wifi_boot/` | New native tests for the boot-counter state machine. |

Files unchanged : `rf.*`, `mqtt.*`, `orchestrator.*`, `nvs_store.cpp` MQTT / remote handling.

## NVS layout extensions

Existing keys (no change) : `wifi.ssid`, `wifi.bssid`, `wifi.channel`.

New keys :

```
wifi.pass         std::string   default ""    WiFi password for STA mode
wifi.boot_count   uint8_t       default 0     incremented on every boot
```

`set_wifi_creds(ssid, pass)` writes `wifi.ssid` + `wifi.pass`, clears `wifi.bssid` and `wifi.channel` (force a fresh scan next boot — the old BSSID hint may point at the wrong router). Atomic w.r.t. the caller : either all 4 keys land in NVS or none do.

`set_boot_count(n)` writes the counter. `get_boot_count()` reads it.

## Boot decision tree

`wifi_manager::init(now_ms)` runs once at startup, before any network activity :

```
                ┌──────────────────────────────────┐
                │ count = nvs.get_boot_count()     │
                │ nvs.set_boot_count(count + 1)    │
                │ schedule reset at                │
                │   now + WIFI_BOOT_STABLE_MS      │
                └──────────────────────────────────┘
                                │
                ┌───────────────┴───────────────┐
                │ count + 1 >= AP_THRESHOLD ?   │
                └───────────────┬───────────────┘
                yes │                          │ no
                    ▼                          ▼
        ┌──────────────────────┐  ┌────────────────────────┐
        │ nvs.set_boot_count(0)│  │ ssid = nvs.get_ssid()  │
        │ start_ap()           │  └────────────┬───────────┘
        └──────────────────────┘               │
                                ┌──────────────┴──────────────┐
                                │ ssid empty ?                │
                                └──────────────┬──────────────┘
                                yes │                       │ no
                                    ▼                       ▼
                          ┌─────────────────┐  ┌──────────────────────┐
                          │ start_ap()      │  │ WiFi.begin(ssid,pass)│
                          │ // first boot   │  │ + BSSID hint if any  │
                          └─────────────────┘  │ retry forever on fail│
                                               └──────────────────────┘
```

The counter is **incremented before** the threshold check : a 4th cold boot within 5 s sees `count + 1 = 4` after writing, takes the AP branch, then resets the counter to 0 so a normal subsequent boot lands back in STA.

The counter is reset to 0 in two cases :
- Threshold reached → AP started → counter reset (avoid getting trapped in AP across normal reboots).
- After `WIFI_BOOT_STABLE_MS` ms of uptime → counter reset (means the box booted normally ; the previous "rapid power-cycle" attempt is over).

`wifi_manager::tick(now_ms)` is called from `loop()` and handles the deferred reset (cheap : one NVS write per uptime epoch, only fires if the counter is non-zero).

## AP mode subsystem

Owned by `captive_portal.cpp`. Started by `wifi_manager::start_ap()` :

```cpp
namespace captive_portal {
  void start(AsyncWebServer& server);   // configures routes, starts DNSServer
  void stop();                          // unregisters routes, stops DNSServer (unused for now: AP reboots)
  void tick(uint32_t now_ms);           // dnsServer.processNextRequest() + AP-timeout reboot
}
```

### SoftAP setup
- `WiFi.mode(WIFI_AP)`.
- `WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0))`.
- `WiFi.softAP(ap_ssid)` — `ap_ssid` is `WIFI_AP_SSID_PREFIX` + the 6-hex chipId suffix already used by `mqtt::client_id()`. Open network.
- `dnsServer.start(53, "*", 192.168.4.1)` — catches every DNS query and points to the portal IP. Triggers the captive-portal pop-up on iOS / Android / macOS / Win11.

### Routes registered on `AsyncWebServer`
- `GET /`            → portal HTML (raw string, ~3 KB).
- `GET /scan.json`   → `[{"ssid":"...","rssi":-65,"secure":true,"channel":6}, ...]`. Uses `WiFi.scanNetworks(false, true)` (sync, hidden=true). Cached for 10 s to avoid hammering the radio on portal-page refresh loops.
- `POST /save`       → JSON body `{"ssid":"...","pass":"..."}`. Validation : SSID non-empty, length ≤ 32 ; pass length ≤ 64. On success : `nvs_store::set_wifi_creds()` → respond 204 → defer `ESP.restart()` 1 s.
- Captive-portal probe URLs (`/generate_204`, `/hotspot-detect.html`, `/connecttest.txt`, `/ncsi.txt`) → redirect to `/`. Speeds up the OS pop-up on devices that probe specific URLs.
- Catch-all → 404. The admin UI routes (`/api/status`, `/api/mqtt`, `/api/remotes`, `/api/factory_reset`) are NOT registered in AP mode.

### AP timeout
- `start_ap()` records `s_ap_started_ms = millis()`.
- `tick()` checks `now_ms - s_ap_started_ms >= WIFI_AP_TIMEOUT_MS` → `ESP.restart()`. The next boot tries STA with whatever creds remain in NVS.
- The timer is consumed by `POST /save` (which restarts immediately anyway) so a user filling the form is never bumped out.

## Admin UI WiFi form (STA mode)

New `<section>` in `web_ui.cpp` between "MQTT broker" and "Remotes" :

```html
<section>
  <h2>WiFi</h2>
  <div class="kv">
    <span>Connected to</span><span id="wifi-current">…</span>
    <span>RSSI</span><span id="wifi-rssi">…</span>
  </div>
  <form id="wifi-form">
    <div class="row"><label>SSID</label><input name="ssid" required maxlength="32"/></div>
    <div class="row"><label>Password</label><input name="pass" type="password" maxlength="64" placeholder="(unchanged)"/></div>
    <div class="actions"><button class="primary" type="submit">Save and reconnect</button></div>
    <div class="msg" id="wifi-msg"></div>
  </form>
</section>
```

- `GET /api/wifi` → `{"ssid":"<current>", "rssi":-65}` — `ssid` is the *connected* SSID (`WiFi.SSID()`), not the stored one (the stored one might match but a debug developer could have flashed something else).
- `POST /api/wifi` → JSON body `{ssid, pass}`. Same validation as `/save`. Same NVS write path. Same deferred restart.
- Empty `pass` field on Save : if SSID matches the current `wifi.ssid` in NVS, keep the existing `wifi.pass`. Otherwise refuse (400 "password required for new SSID").

## Boot counter — pure logic

`include/wifi_boot.h` exposes 3 pure functions, no Arduino headers :

```cpp
namespace wifi_boot {
  // Returns the post-increment counter value (i.e. what was written to NVS).
  uint8_t on_boot(uint8_t stored_count, uint8_t threshold);

  // True iff the post-increment value reached the threshold this boot.
  bool should_force_ap(uint8_t post_increment_count, uint8_t threshold);

  // Returns true if a reset write should be persisted now (uptime >= stable_ms
  // and stored_count > 0). The caller is responsible for writing 0 to NVS.
  bool should_reset(uint8_t stored_count, uint32_t uptime_ms, uint32_t stable_ms);
}
```

Tested in `test/test_wifi_boot/` covering :
- Increment from 0..3, no trigger.
- Increment from 3 → 4 with threshold 4, triggers AP.
- Increment beyond 4 (e.g. stale 5 from a crash mid-write) still triggers AP — `>= threshold`.
- `should_reset` returns false before `stable_ms` regardless of count.
- `should_reset` returns false at any uptime if count is already 0.
- `should_reset` returns true once both conditions hold.

## Module lifecycle in `main.cpp`

```cpp
void setup() {
  nvs_store::init();
  web_ui_pre_init();              // create AsyncWebServer (no routes yet)
  wifi_manager::init(millis());   // boot decision → STA or AP
  if (wifi_manager::is_ap_mode()) {
    captive_portal::start(server);
  } else {
    web_ui::init(server);         // admin UI routes
    mqtt::init();
    orchestrator::init();
  }
  server.begin();
}

void loop() {
  uint32_t now = millis();
  wifi_manager::tick(now);        // counter reset
  if (wifi_manager::is_ap_mode()) {
    captive_portal::tick(now);    // DNS + AP timeout
  } else {
    // existing STA path : mqtt loop, orchestrator tick, etc.
  }
}
```

The dispatch is hard at boot time — we never switch from AP to STA without a reboot. Simpler state machine, no half-initialized MQTT in AP mode, predictable memory layout.

## Failure modes and edge cases

- **NVS write fails during counter increment** : `nvs_store::set_boot_count` returns false. We log it (`logger::warn`) and proceed as if the counter is 0 — graceful degradation, the 4-power-cycle recovery just won't trigger this boot.
- **User saves an SSID that does not exist** : next boot, STA fails, retries forever. The 4-power-cycle path is the recovery.
- **User saves the right SSID with a wrong password** : same as above — STA never associates. 4-power-cycle to recover.
- **Counter overflow** (uint8 wraps to 0 if not reset) : 256 boots without any stable_ms uptime is practically impossible. If it happens, the threshold is not reached → STA path → normal recovery.
- **AP routes leak into STA mode** : avoided by hard dispatch. `captive_portal::start()` is only called in the AP branch ; the routes are owned by the captive-portal module which has its own `server.on(...)` calls.
- **Concurrent `WiFi.scanNetworks()` on the captive portal during STA negotiation** : not an issue — we only scan in AP mode, with no concurrent STA traffic.
- **Browser caching of the portal page** : the portal HTML is served with `Cache-Control: no-store` to ensure subsequent visits re-fetch (especially relevant when the page is opened, the box reboots, the page is reopened from history).
