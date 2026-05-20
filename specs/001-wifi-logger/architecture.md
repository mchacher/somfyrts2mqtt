# Architecture 001

## Touched modules

| File | Role |
|---|---|
| `include/logger.h` + `src/logger.cpp` | Namespace `logger`, `info` / `warn` / `err` with tag prefix |
| `include/wifi_manager.h` + `src/wifi_manager.cpp` | Namespace `wifi`, init + reconnect via ESP32 events |
| `include/secrets.h.example` | Committed template (placeholders `WIFI_SSID`, `WIFI_PASSWORD`) |
| `include/secrets.h` | Real credentials (gitignored) |
| `.gitignore` | Adds `include/secrets.h` |
| `src/main.cpp` | Refactored: init logger then WiFi, empty `loop()` |

No change to `platformio.ini` (WiFi.h ships with the ESP32 Arduino core).

## Decisions

- **`Serial.printf` directly inside the `logger` namespace, no logging framework**. KISS: if we ever need filterable levels or sinks (file, network), we will evolve. For now it is just a tag prefix and an automatic newline.
- **`wifi::init()` is non-blocking**. No `while(!WiFi.isConnected()) delay()`. We register an ESP32 event handler (`WiFi.onEvent`) to react to transitions, and `WiFi.begin()` returns immediately.
- **Auto-reconnect via `WiFi.setAutoReconnect(true)`**: no need to reimplement a state machine, the ESP32 core handles retry on its own. We only log the events.
- **No public `wifi::loop()` work yet**: nothing to do in the main `loop()` for this iter, but the function exists (no-op) so future iters can hook in watchdog or status reporting without touching `main.cpp`.

## Flow

```
setup()
  ├─ Serial.begin(115200); delay(200)
  ├─ logger::info("boot", "hello somfyrts2mqtt")
  └─ wifi::init()
        ├─ WiFi.onEvent(on_wifi_event)
        ├─ WiFi.mode(WIFI_STA)
        ├─ WiFi.setAutoReconnect(true)
        ├─ logger::info("wifi", "connecting ssid=%s", WIFI_SSID)
        └─ WiFi.begin(WIFI_SSID, WIFI_PASSWORD)

on_wifi_event(event)
  ├─ ARDUINO_EVENT_WIFI_STA_GOT_IP        → logger::info("wifi", "connected ip=%s", ip)
  ├─ ARDUINO_EVENT_WIFI_STA_DISCONNECTED  → logger::warn("wifi", "disconnected reason=%d", r)
  └─ (auto-reconnect handled by the core)

loop()
  └─ wifi::loop()  (no-op for now)
```

## Public API

```cpp
// logger.h
namespace logger {
  void info(const char* tag, const char* fmt, ...);
  void warn(const char* tag, const char* fmt, ...);
  void err (const char* tag, const char* fmt, ...);
}

// wifi_manager.h
namespace wifi {
  void init();
  void loop();
  bool is_connected();
}
```
