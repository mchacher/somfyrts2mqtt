# Architecture 003

## Touched modules

| File | Role |
|---|---|
| `include/mqtt.h` + `src/mqtt.cpp` | API + `PubSubClient`-backed impl |
| `test/test_mqtt/test_main.cpp` | Native unit tests for the pure helpers |
| `include/secrets.h.example` | Add `MQTT_BROKER_HOST` / `_PORT` / `_USER` / `_PASS` |
| `src/main.cpp` | Bootstrap MQTT config from secrets → NVS, then `mqtt::init()`, `mqtt::loop()` |
| `platformio.ini` | Add `knolleary/PubSubClient` to `lib_deps` |

## MQTT topic structure

| Topic | Direction | QoS | Retained | Notes |
|---|---|---|---|---|
| `somfy2mqtt/bridge/state` | publish | 0 | yes | `"online"` on connect; LWT sets it to `"offline"` |
| `somfy2mqtt/+/set` | subscribe | 0 | n/a | `<remote_id_hex>` wildcard; payload = `up`/`down`/`stop`/`program` |
| `somfy2mqtt/<id_hex>/state` | publish | 0 | yes | Exposed; called by iter 004 (orchestrator) |
| `somfy2mqtt/<id_hex>/rolling_code` | publish | 0 | yes | Exposed; called by iter 004 |

Topic prefix `somfy2mqtt` comes from `MQTT_TOPIC_PREFIX` in `config.h`.

## Public API

```cpp
#include <cstdint>
#include <cstddef>

namespace mqtt {

  enum class Command : uint8_t {
    Invalid,
    Up,
    Down,
    Stop,
    Program,
  };

  /**
   * @brief Handler signature for parsed incoming commands.
   * @param remote_id  24-bit remote id parsed from the topic.
   * @param cmd        Decoded command (never Invalid; invalid messages are dropped).
   */
  using CommandHandler = void (*)(uint32_t remote_id, Command cmd);

  void init(CommandHandler handler);
  void loop();

  bool is_connected();

  /// Publish a state retained message for a remote. @return false if not connected.
  bool publish_state(uint32_t remote_id, Command last_cmd);

  /// Publish a retained rolling_code update for a remote. @return false if not connected.
  bool publish_rolling_code(uint32_t remote_id, uint16_t code);

  // Pure helpers (inline; testable without PubSubClient)
  Command     parse_command(const char* str, size_t len);
  bool        parse_set_topic(const char* topic, uint32_t& remote_id);
  void        build_state_topic       (uint32_t remote_id, char out[24]);
  void        build_rolling_code_topic(uint32_t remote_id, char out[32]);
  const char* command_to_str(Command cmd);
}
```

## Flow

### Boot
```
setup()
  ├─ logger::info("boot", ...)
  ├─ nvs_store::init()
  ├─ // bootstrap from secrets.h (one-shot if NVS empty)
  │  if (nvs_store::get_mqtt().host.empty()) {
  │    nvs_store::set_mqtt({MQTT_BROKER_HOST, MQTT_BROKER_PORT,
  │                         MQTT_BROKER_USER, MQTT_BROKER_PASS});
  │  }
  ├─ wifi::init()
  └─ mqtt::init(default_log_handler)

loop()
  ├─ wifi::loop()
  └─ mqtt::loop()
```

### `mqtt::loop()`
```
if (!wifi::is_connected())              → return
if (!client.connected()):
  if (millis() - last_attempt >= 5000ms):
    try_connect()
      ├─ build client id = MQTT_CLIENT_PREFIX + chip MAC suffix
      ├─ connect(clientId, user, pass,
      │         willTopic="somfy2mqtt/bridge/state", willQos=0, willRetain=true,
      │         willMessage="offline")
      ├─ on success → publish("somfy2mqtt/bridge/state", "online", retained=true)
      │              subscribe("somfy2mqtt/+/set")
      │              logger::info("mqtt", "connected ...")
      └─ on failure → logger::warn("mqtt", "connect rc=%d", rc)
    last_attempt = millis()
else:
  client.loop()                  // pump
```

### On message
```
on_message(topic, payload, len)
  ├─ uint32_t id
  ├─ if (!parse_set_topic(topic, id))   → drop
  ├─ Command c = parse_command(payload, len)
  ├─ if (c == Command::Invalid)         → drop (warn log)
  └─ handler(id, c)
```

## Validation rules

| Field | Rule |
|---|---|
| Topic prefix | starts with `somfy2mqtt/` |
| Topic shape | `somfy2mqtt/<6-hex>/set` exactly (parts split on `/`) |
| Command payload | one of `up` / `down` / `stop` / `program` (case-insensitive) |
| Payload length | ≤ 16 bytes (anything bigger is rejected) |
