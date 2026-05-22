# Plan 014

## Order of operations

The work splits cleanly along module boundaries. Implementing in this order keeps the build green after each step :

1. **Position estimator** (`include/shutter_state.h`) + its native tests. Pure logic, no deps. Locks down the math.
2. **NVS extensions** (`Remote` + `MqttConfig` fields, name regex, topic regex). Native tests for the validators.
3. **MQTT layer rewrite** (`mqtt.cpp` / `mqtt.h`) : new topic subscribe pattern, new publish helpers, LWT, configurable root topic.
4. **Orchestrator state machine** : per-remote runtime, transitions, 1 Hz tick. Calls into the new MQTT helpers.
5. **Wiring in `main.cpp`** : call `orchestrator::tick()` from `loop()` once per second.
6. **Web UI** : MQTT topic field, duration inputs, Position cell, name regex, drop dead legacy bits.
7. **Build + check + tests + HW validation + PR**.

## Steps in detail

1. `include/shutter_state.h` : write `estimate()` + `should_stop()` as inline functions. No NVS, no Arduino. 30-50 lines.
2. `test/test_shutter_state/test_main.cpp` : ~10 cases covering motion direction, clamping at 0 / 100, mid-travel stop, full Open / Close snap, division-by-zero guard.
3. `include/nvs_store.h` : extend `Remote` and `MqttConfig`. Tighten `is_valid_name()` regex. Add `is_valid_topic()`. Document the wipe-on-flash.
4. `src/nvs_store.cpp` : read / write the new fields. Bump the layout version constant (forces a wipe on boot if the old version is detected).
5. `test/test_nvs/test_main.cpp` : new cases for the name regex + topic regex + position field round-trip.
6. `include/mqtt.h` : drop `Command` / `command_to_str` / `parse_set_topic` / `format_state_topic` / `format_rolling_code_topic`. Add `publish_shutter_state()`, `publish_sensor_aggregated()`, `publish_lwt()`.
7. `src/mqtt.cpp` : rewrite the subscribe path (`cmnd/<topic>/+/+`), parse `<name>` and verb from the topic, dispatch to `orchestrator::handle_*`. Set LWT on connect. Reconnect when `mqtt.topic` changes (signaled by the web UI via a new `mqtt::reconnect_with_new_topic()` call).
8. `test/test_mqtt/test_main.cpp` : update cases ; drop the old topic parser tests, add the new ones (parse a cmnd topic into name + verb + payload).
9. `include/orchestrator.h` / `src/orchestrator.cpp` : add the `ShutterRuntime` table (one entry per remote ; static array sized to `MAX_REMOTES`), the transitions, the 1 Hz `tick()`. The async command queue (iter 011) stays as is, but the orchestrator's tick may enqueue a Stop on intermediate target reached.
10. `src/main.cpp` : compute `now_ms = millis()`. Call `orchestrator::tick(now_ms)` if `now_ms - last_tick >= 1000`. Update `last_tick`.
11. `src/web_ui.cpp` :
    - Add a `Topic` input to the MQTT section (POST `/api/mqtt` body extended).
    - Add `open_dur_s`, `close_dur_s`, `position` to the GET `/api/remotes` JSON.
    - Render the new columns in the table.
    - Add a small `Set` button next to Position that prompts for a 0-100 value (uses `confirm()` + `prompt()` for simplicity, no modal lib).
    - Validate the name input with `pattern="[a-zA-Z0-9_-]{1,32}"`.
    - New endpoints :
      - `POST /api/remotes/<id>/duration` body `{open: 18.5, close: 20.0}` → orchestrator updates NVS + publishes ack.
      - `POST /api/remotes/<id>/position` body `{value: 60}` → orchestrator handles like `Position` cmnd.
      - `POST /api/remotes/<id>/set_position` body `{value: 30}` → manual calibration.
12. `pio run` + `pio check` + `pio test -e native` (all green).
13. HW flash + validation per the matrix below.
14. PR + CI green + merge.

## Test plan

### Native (Unity)

**`test_shutter_state` (new) :**

| Test | Scenario |
|---|---|
| `estimate_no_motion` | start=50, direction=0 → returns 50 regardless of elapsed |
| `estimate_opening_quarter_way` | start=0, duration=20 000 ms, elapsed=5 000 ms, dir=+1 → 25 |
| `estimate_closing_half_way` | start=100, duration=20 000 ms, elapsed=10 000 ms, dir=-1 → 50 |
| `estimate_clamp_max` | elapsed exceeds duration → 100 (opening) |
| `estimate_clamp_min` | elapsed exceeds duration → 0 (closing) |
| `estimate_zero_duration` | duration=0 → returns start (graceful) |
| `should_stop_at_intermediate_target` | estimated=60, target=60, dir=+1 → true |
| `should_stop_overshoot` | estimated=65, target=60, dir=+1 → true |
| `should_stop_undershoot` | estimated=55, target=60, dir=+1 → false |
| `should_stop_full_open_lets_motor_self_stop` | target=100 → false (caller short-circuits the snap) |

**`test_nvs` (extended) :**

| Test | Scenario |
|---|---|
| `name_valid_alpha_underscore_dash` | `kitchen_shutter`, `Bedroom-1`, `K1` → valid |
| `name_invalid_space` | `kitchen shutter` → invalid |
| `name_invalid_special` | `kitchen/shutter`, `bedroom.main`, `+`, `#` → invalid |
| `name_invalid_empty` / `name_invalid_too_long` | → invalid |
| `topic_valid_with_slash` | `home/shutters/bridge1` → valid |
| `topic_invalid_leading_slash` | `/home/...` → invalid |
| `topic_invalid_wildcards` | `home/+/bridge`, `home/#` → invalid |

**`test_mqtt` (rewrite) :**

| Test | Scenario |
|---|---|
| `parse_cmnd_open` | `cmnd/foo-bar/kitchen/Open` → name=kitchen, verb=Open |
| `parse_cmnd_position` | `cmnd/foo-bar/kitchen/Position` → name=kitchen, verb=Position |
| `parse_cmnd_wrong_prefix` | `tele/foo-bar/...`, `cmnd/other/...` → rejected |
| `parse_cmnd_no_verb` | `cmnd/foo-bar/kitchen/` → rejected |

### HW (browser + mosquitto)

Preconditions : factory reset (NVS wipe). Add two remotes `kitchen` (paired with a real shutter) and `bedroom_test` (no real motor, just for telemetry). Set `mqtt.topic = somfyrts2mqtt-test`.

| Case | Action | Expected |
|---|---|---|
| **Calibration** | Web UI : enter Open=18.0, Close=20.0 on `kitchen` row | NVS updated. `stat/somfyrts2mqtt-test/kitchen` shows `{"OpenDuration":18.0,"CloseDuration":20.0}` |
| **Full Open** | `mosquitto_pub -t cmnd/somfyrts2mqtt-test/kitchen/Open` | Shutter goes up. During motion, `tele/.../SENSOR` updates at 1 Hz with Position climbing 0→100, Direction=1, Target=100. On motor self-stop, Position snaps to 100, Direction=0. |
| **Position 50** | `mosquitto_pub -t cmnd/somfyrts2mqtt-test/kitchen/Position -m 50` (from current 100) | Shutter goes down. Telemetry shows Position dropping 100→50, Direction=-1, Target=50. Around 50 (within ±10), the bridge enqueues Stop ; Direction=0 ; shutter stops near mid-travel. |
| **External operation drift** | After calibration, push the physical wall-down button → shutter goes down. Then `Open` via MQTT. | Until the next full Open, Position is stale. After full Open completes, Position snaps to 100 (recalibrated). |
| **LWT on disconnect** | Unplug the device | `tele/somfyrts2mqtt-test/LWT = Offline` (retained) appears within `mqtt.keepalive_s` |
| **Reconnect on topic change** | Web UI : change Topic to `bridge1`, save | Bridge unsubscribes from old prefix, subscribes to `cmnd/bridge1/+/+`, publishes `tele/bridge1/LWT = Online` retained, old `Online` is retained-cleared (publish empty payload retained). |
| **Uncalibrated Position** | Add remote `freshremote`, do not set durations, send `cmnd/.../freshremote/Position 50` | `stat/.../freshremote` = `{"error":"not calibrated"}` ; no RF emission. |
| **SetPosition** | `cmnd/.../kitchen/SetPosition 25` | NVS Position becomes 25 without RF. `stat/.../kitchen` ack. |
| **Aggregated SENSOR** | While `kitchen` is moving and `bedroom_test` is at rest | SENSOR JSON contains both keys ; `bedroom_test` has Direction=0 and last known Position. |

## Risks

- **Position drift via gear / motor wear** : duration drifts over months. Mitigation : full Open / Close recalibrates. Long-term : an iter 015 could auto-suggest recalibration if the user has not done a full Open for N days.
- **External operation invisible** : same physical limitation we discussed. Documented in the spec.
- **Stop overshoot for intermediate targets** : the bridge sends Stop when estimated reaches target, but the motor's stop latency adds ~200-500 ms of overshoot. Acceptable for v1 ; iter 015 could tune a "preempt" offset.
- **NVS wipe surprises** : we accept it (dev stage). Users testing on existing boards must factory-reset before flashing iter 014.

## What we are NOT changing

- The RF emission path. iter 006 + iter 011 short path remains the only RF code.
- The async command queue (iter 011). New Stop emissions from the position tick go through it.
- The WiFi manager (iter 009 + iter 011 sticky-bad-BSSID recovery).
- The Erase confirm modal (iter 013).
