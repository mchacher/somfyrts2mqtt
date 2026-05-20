# Architecture 007

## Touched modules

| File | Role |
|---|---|
| `include/web_ui.h` + `src/web_ui.cpp` | `AsyncWebServer`, routes, JSON handlers, embedded HTML |
| `include/mqtt.h` + `src/mqtt.cpp` | Add `mqtt::disconnect()` |
| `src/main.cpp` | Call `web_ui::init()` after `wifi::init()` |
| `platformio.ini` | Add `esp32async/AsyncTCP`, `esp32async/ESPAsyncWebServer`, `bblanchon/ArduinoJson@^7` |

## REST API

| Method | Path | Body | Response |
|---|---|---|---|
| `GET` | `/` | — | HTML page (text/html, `PROGMEM`) |
| `GET` | `/api/status` | — | `{version, ip, mac, uptime_s, mqtt_connected, remotes_count}` |
| `GET` | `/api/mqtt` | — | `{host, port, user}` (no `pass` returned) |
| `POST` | `/api/mqtt` | `{host, port, user, pass}` | `204 No Content` (writes NVS, triggers `mqtt::disconnect()`) |
| `GET` | `/api/remotes` | — | `[{id_hex, rolling_code, name}, ...]` |
| `POST` | `/api/remotes` | `{id_hex, name}` | `201 Created` with the new entry, or `400` / `409` on validation / capacity |
| `DELETE` | `/api/remotes/<id_hex>` | — | `204 No Content`, or `404` |
| `POST` | `/api/factory_reset` | — | `204 No Content` then `ESP.restart()` after a 500 ms delay |

All POST/PATCH bodies are JSON (`Content-Type: application/json`).

## Public API

```cpp
// web_ui.h
namespace web_ui {
  /// Start the AsyncWebServer. Idempotent.
  void init();
}

// mqtt.h (addition)
namespace mqtt {
  // ... existing ...
  /// Drop the current MQTT connection; reconnect happens on the next loop() tick.
  void disconnect();
}
```

## Flow

```
setup()
  ├─ logger::info("boot", ...)
  ├─ nvs_store::init()
  ├─ bootstrap_mqtt_from_secrets()
  ├─ wifi::init()
  ├─ mqtt::init(<stub handler for now>)
  └─ web_ui::init()                        ← new

loop()
  ├─ wifi::loop()
  └─ mqtt::loop()
  // AsyncWebServer runs on its own task; no work needed in loop()
```

```
POST /api/mqtt with {host, port, user, pass}
  └─ web_ui handler
       ├─ parse JSON
       ├─ validate (host non-empty, port 1..65535)
       ├─ nvs_store::set_mqtt(cfg)         → persists
       ├─ mqtt::disconnect()               → reconnect loop picks up the new config
       └─ respond 204
```

## HTML structure (PROGMEM)

Single page, three `<section>` blocks (status, mqtt, remotes), one `<footer>` with the factory reset button. Vanilla JS calls the JSON endpoints on load and on form submit. ~150 lines including inline CSS.

## Validation rules

| Field | Rule | Failure response |
|---|---|---|
| MQTT host | non-empty, ≤ 64 chars | `400 Bad Request` |
| MQTT port | 1..65535 | `400 Bad Request` |
| Remote id_hex | exactly 6 hex chars (case-insensitive) | `400 Bad Request` |
| Remote name | 1..32 chars, ASCII printable | `400 Bad Request` |
| Capacity | ≤ 16 remotes | `409 Conflict` |
| Delete missing | id absent from NVS | `404 Not Found` |

## Failure modes

- AsyncWebServer fails to start (port busy, OOM) → `logger::err`, server stays disabled. Boot continues normally — the MQTT side still works.
- JSON parse error on a POST body → `400` with `{"error": "..."}`.
- `nvs_store::set_*` returns false → `500 Internal Server Error` + log.
