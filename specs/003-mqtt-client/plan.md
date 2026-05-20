# Plan 003

## Steps

1. Add `knolleary/PubSubClient` to `platformio.ini` `lib_deps`.
2. Extend `include/secrets.h.example` with `MQTT_BROKER_HOST/PORT/USER/PASS` placeholders.
3. Create `include/mqtt.h` with the public API + inline pure helpers (Doxygen blocks).
4. Implement the pure helpers (`parse_command`, `parse_set_topic`, `build_state_topic`, `build_rolling_code_topic`, `command_to_str`).
5. Write `test/test_mqtt/test_main.cpp` covering the helpers. Verify with `pio test -e native`.
6. Implement `PubSubClient`-backed wiring in `src/mqtt.cpp`: connect, reconnect, subscribe, dispatch, publish helpers.
7. Wire `src/main.cpp`: NVS bootstrap from `secrets.h`, then `mqtt::init(default_handler)` after `wifi::init()`; `mqtt::loop()` in `loop()`.
8. Update local `include/secrets.h` with real broker creds for HW testing.
9. Build (`pio run`), check (`pio check`), test (`pio test -e native`) — all green.
10. Flash + HW test below.

## Test plan

### Native (Unity)

| Test | Scenarios |
|---|---|
| `parse_command_valid` | "up"→Up, "DOWN"→Down (case-insensitive), "stop"→Stop, "program"→Program |
| `parse_command_invalid` | ""→Invalid, "upx"→Invalid, "stopping"→Invalid, nullptr→Invalid, 17-byte payload→Invalid |
| `parse_set_topic_valid` | "somfy2mqtt/A1B2C3/set" → id=0xA1B2C3 |
| `parse_set_topic_wrong_prefix` | "other/A1B2C3/set" → false |
| `parse_set_topic_wrong_suffix` | "somfy2mqtt/A1B2C3/state" → false |
| `parse_set_topic_bad_hex` | "somfy2mqtt/XYZABC/set" → false |
| `parse_set_topic_empty_id` | "somfy2mqtt//set" → false |
| `parse_set_topic_short_id` | "somfy2mqtt/A1B/set" → false |
| `build_state_topic` | id=0xA1B2C3 → "somfy2mqtt/A1B2C3/state" |
| `build_rolling_code_topic` | id=0xA1B2C3 → "somfy2mqtt/A1B2C3/rolling_code" |
| `command_to_str` | Up→"up", Down→"down", Stop→"stop", Program→"program", Invalid→"" |

### HW (manual)

| Case | Action | Expected |
|---|---|---|
| **Cold boot, MQTT connect** | Fresh-NVS device, flash with broker in `secrets.h`. Monitor serial. | `[mqtt] connecting host=<x> port=<y>` → `[mqtt] connected` → `[mqtt] subscribed somfy2mqtt/+/set` |
| **Bridge state retained** | Subscribe to `somfy2mqtt/bridge/state` (MQTT Explorer or `mosquitto_sub -t 'somfy2mqtt/bridge/state' -v`) | Receives `online` (retained) |
| **Dispatch incoming command** | `mosquitto_pub -t 'somfy2mqtt/A1B2C3/set' -m 'up'` | Serial logs `[mqtt] cmd id=A1B2C3 cmd=up` |
| **LWT on disconnect** | Pull USB cable, wait ~30s | `somfy2mqtt/bridge/state` flips to `offline` (retained) |
| **Reconnect after broker restart** | Restart mosquitto on sowelox | Serial logs `[mqtt] disconnected` then `[mqtt] connected` within ~5 s |
