# Architecture 014

## Touched modules

| File | Change |
|---|---|
| `include/nvs_store.h`, `src/nvs_store.cpp` | Extend `Remote` with `open_time_ms`, `close_time_ms`, `position`. Add `mqtt.topic` (root topic prefix) to `MqttConfig`. Tighten `is_valid_name()` to the new regex. NVS layout bump (acceptable per iter 014 wipe-on-flash). |
| `include/orchestrator.h`, `src/orchestrator.cpp` | Per-remote position state machine. New `set_position(N)`, `set_open_duration(ms)`, `set_close_duration(ms)`, `set_calibration_position(N)` entry points. 1 Hz tick driven from `loop()` updates estimates + enqueues Stop on intermediate target reached + publishes telemetry. |
| `include/mqtt.h`, `src/mqtt.cpp` | Replace `somfy2mqtt/<HEXID>/...` subscribe with `cmnd/<topic>/+/+` wildcard. New `publish_shutter_state(name, position, direction, target)`, `publish_sensor_aggregated()`, `publish_lwt(online)`. Subscribe + publish use `mqtt.topic` from NVS. Reconnect on topic change. |
| `src/web_ui.cpp` | Add `mqtt.topic` field to the broker form. Add `open_dur_s`, `close_dur_s` columns + `Position` cell + `Set Position` input per remote row. Name input gets the regex pattern attribute. |
| `include/shutter_state.h` | New pure-logic header for the position estimator (testable on native). |
| `test/test_shutter_state/` | New native tests for the estimator + name validator + topic-prefix validator. |

Files unchanged : `rf.cpp` / `rf.h` (short path stays as is ; long-press path was rolled back with iter 012), `wifi_manager.cpp`, `main.cpp` (except adding the orchestrator tick call).

## NVS layout extensions

`Remote` struct (currently `id`, `name`, `rolling_code`) gains :

```cpp
uint32_t open_time_ms;     // 0 = uncalibrated, otherwise duration of a full Open
uint32_t close_time_ms;    // 0 = use open_time_ms as a fallback, otherwise duration of a full Close
uint8_t  position;         // 0-100, last persisted snapshot
```

Persistence keyed under `remotes/<hex_id>/...` as today, with three new sub-keys.

`MqttConfig` (currently `host`, `port`, `user`, `pass`) gains :

```cpp
std::string topic;  // root topic prefix, default = "somfyrts2mqtt-<MAC suffix>"
```

Stored under `mqtt/topic`.

## MQTT topic structure

Given `<topic>` = `mqtt.topic` from NVS (default `somfyrts2mqtt-XXXXXX`) and `<name>` = a Remote's `name` :

**Subscribed (cmnd) :**

```
cmnd/<topic>/<name>/Open              ""              → orchestrator: open
cmnd/<topic>/<name>/Close             ""              → orchestrator: close
cmnd/<topic>/<name>/Stop              ""              → orchestrator: stop
cmnd/<topic>/<name>/Position          "<0-100>"       → orchestrator: move to N %
cmnd/<topic>/<name>/OpenDuration      "<seconds>"     → NVS update + ack
cmnd/<topic>/<name>/CloseDuration     "<seconds>"     → NVS update + ack
cmnd/<topic>/<name>/SetPosition       "<0-100>"       → NVS update (no RF)
```

Subscription pattern uses a single multi-level wildcard : `cmnd/<topic>/+/+`. The handler parses `<name>` and the trailing verb.

**Published (tele / stat) :**

```
tele/<topic>/LWT                       "Online"|"Offline"   retained, LWT="Offline"
tele/<topic>/SENSOR                    {JSON aggregated, see below}
stat/<topic>/<name>                    {JSON per remote, see below}
```

`tele/<topic>/SENSOR` payload (one entry per remote ; published 1 Hz during any motion + on every state transition) :

```json
{
  "kitchen_shutter": {"Position": 45, "Direction": 1, "Target": 100},
  "bedroom":         {"Position":  0, "Direction": 0, "Target":   0}
}
```

`stat/<topic>/<name>` payload (published after each cmnd as ack) :

```json
{"Position": 45, "Direction": 1, "Target": 100}
```

Or on a duration update :

```json
{"OpenDuration": 18.5, "CloseDuration": 20.2}
```

Or on error :

```json
{"error": "not calibrated"}
```

Convention :
- **Position** 0-100, where 0 = closed and 100 = open.
- **Direction** -1 = closing, 0 = stopped, 1 = opening (Tasmota convention).
- **Target** 0-100, the destination of the current or last motion.
- **OpenDuration / CloseDuration** float seconds (one decimal) on the wire.

## Position estimator

Pure-logic, in `include/shutter_state.h` for native testability :

```cpp
namespace shutter_state {
  // Returns the estimated position (0-100) at `now_ms`, given the motion
  // start time, the start position, the direction (-1 / +1) and the
  // calibrated travel duration (ms). Clamped to [0, 100].
  uint8_t estimate(uint32_t motion_started_ms,
                   uint32_t now_ms,
                   uint8_t  start_position,
                   int8_t   direction,
                   uint32_t duration_ms);

  // True if a motion towards `target` should stop now given the current
  // estimate. Strict for intermediate targets (1-99) ; lenient for 0/100
  // (motor self-stops, we let it).
  bool should_stop(uint8_t estimated, uint8_t target, int8_t direction);
}
```

## Orchestrator state machine

Per remote (in RAM only ; rebuilt on boot) :

```cpp
struct ShutterRuntime {
  uint8_t  state;             // 0 = idle, 1 = opening, 2 = closing
  uint8_t  position;          // 0-100, live estimate during motion
  uint8_t  target;            // 0-100, destination of current motion
  uint32_t motion_started_ms; // millis() at the RF Up/Down emission
  uint8_t  start_position;    // position at motion_started_ms
};
```

Transitions :

- `open()`  : send RF Up, state=1, target=100, start_position=position, motion_started_ms=now
- `close()` : send RF Down, state=2, target=0, ...
- `stop()`  : send RF Stop, state=0, freeze position (current estimate), persist to NVS
- `set_position(N)` : if `open_time_ms == 0` and direction-relevant duration unknown, return error. Else compute direction (open if N > position, close if N < position), send RF Up/Down, state=opening/closing, target=N
- `set_calibration_position(N)` : update `position` in NVS only, no RF, ack

Tick (1 Hz, from `loop()`) :

For each remote in motion :
1. estimated = `shutter_state::estimate(...)`
2. If `target ∈ {0, 100}` and estimated reached target → state=idle, snap position to 0/100, persist NVS, publish stat ack.
3. Else if `shutter_state::should_stop(estimated, target, direction)` → enqueue Stop emission for this remote, state will transition on Stop handler.
4. Publish `tele/<topic>/SENSOR` aggregated JSON.

## Web UI changes

- MQTT section : new field `Topic` editable, validated by the same regex as the prefix.
- Remotes table : new columns `Open dur (s)`, `Close dur (s)` with inline `<input type="number" step="0.1">`, on blur PUTs the new value. New `Position` column with current value + a `Set` button opening a small number input (0-100) that issues `SetPosition`.
- Name input on the add form : `pattern="[a-zA-Z0-9_-]{1,32}"` + `title` tooltip explaining the constraint.

## Telemetry frequency

- During motion : 1 Hz tick → `tele/<topic>/SENSOR` aggregated.
- On state change (cmd issued, motion start, motion stop) : immediate `stat/<topic>/<name>` + immediate `tele/<topic>/SENSOR`.
- At rest : no periodic publish.

## LWT / online presence

In `mqtt.cpp` `connect()` :

```cpp
client.connect(client_id,
               cfg.user.c_str(), cfg.pass.c_str(),
               lwt_topic, /*qos*/ 0, /*retain*/ true, "Offline");
```

On `connect()` success, immediately publish `Online` retained to the same topic. On graceful disconnect, publish `Offline` retained.

## What we drop

The legacy `somfy2mqtt/<HEXID>/{set,state,rolling_code}` topics are removed entirely. Acceptable because no integration was developed against them. The web UI command endpoints (POST `/api/remotes/<id>/<verb>`) stay as the test interface.
