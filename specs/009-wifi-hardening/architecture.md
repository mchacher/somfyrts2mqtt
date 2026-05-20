# Architecture 009

## Touched modules

| File | Change |
|---|---|
| `include/wifi_manager.h` | No public API change |
| `src/wifi_manager.cpp` | Full rewrite: hardening + scan-pin-best-BSSID + NVS hint reuse + hint invalidation |
| `include/nvs_store.h` | Add `WifiHint` struct + `get_wifi_hint()` / `set_wifi_hint()` / `clear_wifi_hint()` |
| `src/nvs_store.cpp` | Implement the three new helpers ; store BSSID as 12-char uppercase hex, channel as uint8, SSID as string |
| `src/main.cpp` | Bump the WiFi wait from 5 s to 15 s |

No change to `platformio.ini` or CI.

## NVS layout addition

In the `somfy` namespace:

| Key | Type | Notes |
|---|---|---|
| `wifi.bssid` | string (12 hex chars) | e.g. `"3C64CFB45076"` |
| `wifi.channel` | `uint8_t` | 1..14 |
| `wifi.ssid` | string | The SSID the hint applies to (used to invalidate the hint when secrets.h changes) |

## Public API addition (nvs_store)

```cpp
namespace nvs_store {
  struct WifiHint {
    std::string ssid;
    uint8_t     bssid[6];
    uint8_t     channel;
  };

  bool get_wifi_hint(WifiHint& out);   ///< @return false if absent or empty
  bool set_wifi_hint(const WifiHint& hint);
  void clear_wifi_hint();
}
```

## Flow

```
wifi::init()
  ├─ derive hostname from MAC
  ├─ WiFi.persistent(false), mode(STA), setHostname, setSleep(false), setAutoReconnect(true)
  ├─ WiFi.onEvent(on_event)
  │
  ├─ if (nvs_store::get_wifi_hint(hint) && hint.ssid == WIFI_SSID):
  │     // FAST PATH
  │     logger::info("wifi", "fast path bssid=%s ch=%u", hex, hint.channel)
  │     WiFi.begin(WIFI_SSID, WIFI_PASSWORD, hint.channel, hint.bssid)
  │     return
  │
  └─ // SLOW PATH (first boot or stale hint)
     scan + pick best BSSID matching WIFI_SSID
     if found:
       nvs_store::set_wifi_hint({...})
       WiFi.begin(WIFI_SSID, WIFI_PASSWORD, best.channel, best.bssid)
     else:
       WiFi.begin(WIFI_SSID, WIFI_PASSWORD)   // last-resort default

on_event:
  ARDUINO_EVENT_WIFI_STA_GOT_IP        → log "connected ip=… rssi=… in Xms"
  ARDUINO_EVENT_WIFI_STA_DISCONNECTED  →
     decode reason ;
     if reason in {BEACON_TIMEOUT, NO_AP_FOUND, ASSOC_FAIL}:
       nvs_store::clear_wifi_hint()   // next boot will rescan
     log "disconnected reason=N (NAME)"
```

## Logs (sample)

First boot (slow path):
```
[wifi] no hint, scanning 2.4 GHz...
[wifi] 7 APs visible, target 'GrangeNeuve_Garage_2.4GHz' best bssid=3C:64:CF:B4:50:76 ch=2 rssi=-69
[wifi] pinning bssid=3C:64:CF:B4:50:76 ch=2 hostname=somfyrts2mqtt-94:91:E8
[wifi] connected ip=192.168.0.75 rssi=-70 in 2744ms
```

Subsequent boot (fast path):
```
[wifi] hint bssid=3C:64:CF:B4:50:76 ch=2 hostname=somfyrts2mqtt-94:91:E8
[wifi] connected ip=192.168.0.75 rssi=-70 in 1840ms
```

## Failure handling

| Disconnect reason | Action |
|---|---|
| 2 `AUTH_EXPIRE`, 4 `ASSOC_EXPIRE`, 15 `4WAY_HANDSHAKE_TIMEOUT`, 204 `HANDSHAKE_TIMEOUT` | Log only — the core's `setAutoReconnect(true)` retries on the same BSSID. |
| 200 `BEACON_TIMEOUT`, 201 `NO_AP_FOUND`, 203 `ASSOC_FAIL` | Log + `nvs_store::clear_wifi_hint()` so the **next boot** does a fresh scan. The in-progress connection lets the core retry — we don't trigger an in-band rescan ourselves (keeps the code simple). |
