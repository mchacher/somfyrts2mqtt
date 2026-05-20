# Architecture 004

## Touched modules

| File | Role |
|---|---|
| `include/rf.h` + `src/rf.cpp` | Stub `rf::init` and `rf::send_somfy` (logs only) |
| `include/orchestrator.h` + `src/orchestrator.cpp` | `handle_command(remote_id, cmd)` wiring NVS + RF + MQTT publish |
| `test/test_orchestrator/test_main.cpp` | Native tests for the `command_to_button` mapping |
| `src/main.cpp` | Call `rf::init()`, register `orchestrator::handle_command` as the MQTT handler (replaces the stub) |

No change to `platformio.ini`.

## Somfy button codes

Standard Somfy RTS remote button bitmap (datasheet for the original RTS frame):

| `mqtt::Command` | Somfy button | Value |
|---|---|---|
| `Up` | UP | `0x02` |
| `Down` | DOWN | `0x04` |
| `Stop` | MY (centre / stop) | `0x01` |
| `Program` | PROG (rear of motor) | `0x80` |
| `Invalid` | — | `0x00` (rejected upstream) |

## Public APIs

```cpp
// rf.h
namespace rf {
  bool init();
  bool send_somfy(uint32_t remote_id, uint16_t rolling_code, uint8_t button);
}

// orchestrator.h
#include "mqtt.h"
namespace orchestrator {
  void handle_command(uint32_t remote_id, mqtt::Command cmd);

  // pure helper (inline; testable without NVS/MQTT)
  inline uint8_t command_to_button(mqtt::Command cmd);
}
```

## Flow

```
mqtt::on_message("somfy2mqtt/A1B2C3/set", "up")
  └─ orchestrator::handle_command(0xA1B2C3, Command::Up)
       ├─ nvs_store::get_remote(0xA1B2C3, remote)              ← may fail → drop + warn
       ├─ uint8_t button = command_to_button(Command::Up)      ← 0x02
       ├─ uint16_t next  = remote.rolling_code + 1
       ├─ nvs_store::update_rolling_code(0xA1B2C3, next)       ← persisted FIRST
       ├─ rf::send_somfy(0xA1B2C3, next, button)               ← stub logs, returns true
       ├─ mqtt::publish_state(0xA1B2C3, Command::Up)           ← retained "up"
       ├─ mqtt::publish_rolling_code(0xA1B2C3, next)           ← retained "<next>"
       └─ logger::info("orch", "executed up on A1B2C3 code=<next>")
```

## Boot sequence

```
setup()
  ├─ Serial.begin
  ├─ logger::info("boot", ...)
  ├─ nvs_store::init()
  ├─ bootstrap_mqtt_from_secrets()
  ├─ wifi::init()
  ├─ mqtt::init(orchestrator::handle_command)                   ← was a stub handler
  ├─ rf::init()                                                  ← new
  ├─ (wait briefly for WiFi)
  └─ web_ui::init()
```

## Failure handling

| Failure | Behaviour |
|---|---|
| Unknown remote id | `logger::warn("orch", "unknown remote %06X, drop")`. No NVS / RF / MQTT side effect. |
| `update_rolling_code` fails | `logger::err`. **No RF call** (we refuse to emit an unpersisted code). |
| `rf::send_somfy` returns false | `logger::err`. Rolling code is already incremented (persistence-before-emit); we accept losing one code. |
| `mqtt::publish_state` / `publish_rolling_code` returns false | Logged at `warn` but does not block. State recovery happens at next dispatch. |
