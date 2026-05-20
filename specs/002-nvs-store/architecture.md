# Architecture 002

## Touched modules

| File | Role |
|---|---|
| `include/nvs_store.h` + `src/nvs_store.cpp` | API + `Preferences`-backed implementation |
| `test/test_nvs/test_nvs.cpp` | Native unit tests for the pure helpers |
| `src/main.cpp` | Call `nvs_store::init()` after `logger`, before `wifi` |
| `platformio.ini` | No change (`Preferences` ships with the ESP32 Arduino core) |

## NVS layout

Single `Preferences` namespace: **`somfy`**.

| Key | Type | Notes |
|---|---|---|
| `schema` | `uint8_t` | Schema version sentinel (currently `1`) |
| `mqtt.host` | string | |
| `mqtt.port` | `uint16_t` | |
| `mqtt.user` | string | Optional |
| `mqtt.pass` | string | Optional |
| `r.<HEXID>.code` | `uint16_t` | `<HEXID>` = 6 uppercase hex chars |
| `r.<HEXID>.name` | string | 1..32 chars |
| `r.index` | string | CSV of hex ids, e.g. `"A1B2C3,D4E5F6"` |

NVS key max length is 15 chars. `r.A1B2C3.code` = 13 chars ✓.

## Public API

```cpp
#include <cstdint>
#include <cstddef>
#include <string>

namespace nvs_store {

  struct MqttConfig {
    std::string host;
    uint16_t    port = 1883;
    std::string user;
    std::string pass;
  };

  struct Remote {
    uint32_t    id;            // 24-bit (validated)
    uint16_t    rolling_code;
    std::string name;
  };

  void init();
  bool ready();

  // MQTT config
  bool       set_mqtt(const MqttConfig& cfg);
  MqttConfig get_mqtt();

  // Remotes
  bool   add_remote(uint32_t id, uint16_t code, const std::string& name);
  bool   update_rolling_code(uint32_t id, uint16_t new_code);
  bool   delete_remote(uint32_t id);
  bool   get_remote(uint32_t id, Remote& out);
  size_t list_remotes(Remote* out, size_t max_count);
  size_t remotes_count();

  // Admin
  void factory_reset();

  // Pure helpers (exposed for native tests)
  bool is_valid_id(uint32_t id);
  bool is_valid_name(const std::string& name);
  void format_id_hex(uint32_t id, char out[7]);   // writes 6 chars + NUL
  bool parse_id_hex(const char* in, uint32_t& out);
  bool index_contains(const std::string& csv, const char* id_hex);
  void index_add    (std::string& csv,        const char* id_hex);
  bool index_remove (std::string& csv,        const char* id_hex);

}
```

## Flow

```
nvs_store::init()
  ├─ Preferences.begin("somfy", false)
  ├─ schema = read uint8 "schema" (default 0)
  ├─ if (schema == 0)        → write 1, log "fresh init, schema=1"
  ├─ else if (schema != 1)   → logger::err, s_ready stays false, return
  ├─ s_ready = true
  └─ logger::info("nvs", "ready schema=1 remotes=%u", count)

nvs_store::add_remote(id, code, name)
  ├─ validate id + name
  ├─ if remotes_count() == 16 and id not already known   → return false
  ├─ format_id_hex(id, hex)
  ├─ Preferences.putUShort("r.<hex>.code", code)
  ├─ Preferences.putString("r.<hex>.name", name)
  ├─ idx = read "r.index" ; index_add(idx, hex) ; write back
  └─ log "[nvs] remote +<hex> code=%u name=%s"
```

## Validation rules

| Field | Rule |
|---|---|
| `id` | `0 < id ≤ 0xFFFFFF` (24-bit; `0` reserved as "invalid") |
| `name` | 1..32 chars, ASCII printable |
| `rolling_code` | any `uint16_t` (no check; wraps naturally) |
| `mqtt.port` | any `uint16_t`; the MQTT client (iter 003) rejects `0` |
| count | ≤ 16 simultaneous remotes |
