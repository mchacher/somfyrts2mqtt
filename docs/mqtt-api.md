# MQTT API

The bridge speaks the **Tasmota Shutter MQTT subset**, keyed by remote name (not by Tasmota's integer index). Most Tasmota / ESPHome / Home Assistant tools work out of the box.

## Topic structure

With `<root>` = the value of `mqtt.topic` from the admin UI (default `somfyrts2mqtt`) and `<name>` = a registered remote's name :

```
Commands (you publish to the bridge)
  cmnd/<root>/<name>/Open                ""          → move to 100 %
  cmnd/<root>/<name>/Close               ""          → move to 0 %
  cmnd/<root>/<name>/Stop                ""          → stop current motion
  cmnd/<root>/<name>/Position            0..100      → move to N %
  cmnd/<root>/<name>/OpenDuration        <seconds>   → set full-Open time
  cmnd/<root>/<name>/CloseDuration       <seconds>   → set full-Close time
  cmnd/<root>/<name>/SetPosition         0..100      → mark current position (no RF)
  cmnd/<root>/<name>/Toggle              ""          → Gate: emit the Somfy 80-bit Toggle frame (single-button cycle, since v0.5.0)
  cmnd/<root>/Status                     ""          → force a fresh SENSOR publish (since v0.2.0)

State (bridge publishes)
  tele/<root>/LWT                        "Online" | "Offline"   (retained + LWT)
  tele/<root>/SENSOR                     JSON, retained, on connect + every 1 s during motion + on state change
  stat/<root>/<name>                     JSON, ack after each cmnd
```

The bridge subscribes with two patterns : `cmnd/<root>/+/+` for per-remote commands (adding a new remote is a NVS operation, no re-subscribe needed), and `cmnd/<root>/Status` for the bridge-wide resync command.

**SENSOR is retained since v0.2.0.** A fresh subscriber to `tele/<root>/SENSOR` receives the latest snapshot of every paired remote immediately on subscribe — no need to wait for the next motion or to issue a Status command. Tools like the Sowel `sowel-plugin-somfy-rts` integration rely on this for discovery.

## Payloads

### `tele/<root>/SENSOR` (aggregated, retained, 1 Hz during motion)

```json
{
  "kitchen": {"Position": 45, "Direction": 1, "Target": 100},
  "bedroom": {"Position": 0,  "Direction": 0, "Target": 0},
  "driveway": {"Position": 0, "Direction": 0, "Target": 0, "Type": "gate"}
}
```

| Field | Range | Meaning |
|---|---|---|
| `Position` | 0..100 | Current estimated position. 0 = closed, 100 = open (or extended for awnings) |
| `Direction` | -1, 0, 1 | -1 closing, 0 idle, 1 opening (Tasmota convention) |
| `Target` | 0..100 | Destination of the current or last motion |
| `Type` | string | **Optional (iter 022).** Device type when it is *not* a shutter — currently `"gate"`. **Absent for shutters**, so a pure-shutter deployment is byte-identical to before. A gate is blind (no feedback): `Position`/`Direction`/`Target` are present but meaningless — ignore them for a gate. |

**Device types.** By default every remote is a roller shutter (time-based
position). A remote can be switched to another RTS equipment type in the admin
UI (Remotes → ⚙ → Type). A **Gate** ("portail coulissant") drops the time-based
position (a gate is blind — no feedback) and exposes **four** commands:
`Open` / `Close` / `Stop` (which emit Up / Down / My) **and** `Toggle`. The
Toggle is the Somfy single-button command — a dedicated **80-bit RTS frame**
(not a normal button byte), which the bridge encodes and transmits itself. A
single-button gate motor cycles open → stop → close → stop on `Toggle`;
`Open`/`Close`/`Stop` also work if the motor honours them separately. A gate advertises `"Type":"gate"` so a consumer
can present it as an HA `cover` with `device_class: gate`. The bridge only emits
the hint + the verbs; the device-class mapping is the client's job.

### `stat/<root>/<name>` (per-cmnd ack)

After Open / Close / Stop / Position / SetPosition :

```json
{"Position": 45, "Direction": 1, "Target": 100}
```

After OpenDuration / CloseDuration :

```json
{"OpenDuration": 18.5, "CloseDuration": 20.0}
```

On error (e.g. uncalibrated remote receives a `Position 50`) :

```json
{"error": "not calibrated"}
```

### `tele/<root>/LWT`

| Payload | When |
|---|---|
| `Online` | Published with retain=true immediately after connecting to the broker |
| `Offline` | Published by the broker via the MQTT Will mechanism when the bridge disconnects ungracefully ; or by the bridge with retain=true right before a planned disconnect |

## Examples (mosquitto_pub / sub)

```bash
# Move kitchen shutter all the way up
mosquitto_pub -h 192.168.0.230 -t cmnd/somfyrts2mqtt/kitchen/Open -m ""

# Set kitchen Open duration to 18 s
mosquitto_pub -h 192.168.0.230 -t cmnd/somfyrts2mqtt/kitchen/OpenDuration -m 18

# Move kitchen to 50 % (motor moves, bridge auto-stops near 50)
mosquitto_pub -h 192.168.0.230 -t cmnd/somfyrts2mqtt/kitchen/Position -m 50

# Mark kitchen as currently at 30 % (e.g. you used a physical remote ; resync)
mosquitto_pub -h 192.168.0.230 -t cmnd/somfyrts2mqtt/kitchen/SetPosition -m 30

# Force the bridge to republish SENSOR right now (since v0.2.0)
mosquitto_pub -h 192.168.0.230 -t cmnd/somfyrts2mqtt/Status -m ""

# Watch every state change
mosquitto_sub -h 192.168.0.230 -t "tele/somfyrts2mqtt/#" -v
mosquitto_sub -h 192.168.0.230 -t "stat/somfyrts2mqtt/#" -v
```

## Python CLI

A friendly wrapper sits in [`tools/mqtt-cli.py`](../tools/mqtt-cli.py). It uses `paho-mqtt` and a per-tools venv (kept out of git).

```bash
# One-time setup
python3 -m venv tools/.venv
tools/.venv/bin/pip install -r tools/requirements.txt

# Configure once via env
export MQTT_BROKER=192.168.0.230
export MQTT_ROOT=somfyrts2mqtt

# Then drive the bridge in one-liners
tools/.venv/bin/python tools/mqtt-cli.py open kitchen
tools/.venv/bin/python tools/mqtt-cli.py position kitchen 50
tools/.venv/bin/python tools/mqtt-cli.py open-dur kitchen 18.5
tools/.venv/bin/python tools/mqtt-cli.py watch           # live-tail tele + stat
tools/.venv/bin/python tools/mqtt-cli.py list            # quick table of all shutters
```

`tools/mqtt-cli.py --help` lists every subcommand.

## Home Assistant

Auto-discovery via Tasmota Discovery is not implemented yet (planned for a future iter). For now, declare each shutter manually in `configuration.yaml` :

```yaml
mqtt:
  cover:
    - name: "Kitchen shutter"
      command_topic: cmnd/somfyrts2mqtt/kitchen/Position
      position_topic: tele/somfyrts2mqtt/SENSOR
      position_template: "{{ value_json.kitchen.Position }}"
      set_position_topic: cmnd/somfyrts2mqtt/kitchen/Position
      payload_open:  "100"
      payload_close: "0"
      payload_stop:  ""    # use a Stop helper or a service instead
      position_open: 100
      position_closed: 0
      optimistic: false
      availability:
        - topic: tele/somfyrts2mqtt/LWT
          payload_available: Online
          payload_not_available: Offline
```

## Multi-bridge

If you run two bridges on the same broker, set distinct **Topic** values in each one's admin UI (e.g. `somfy-rdc` and `somfy-etage`). They share the broker but their topics never collide.

The MQTT client_id is derived from the Topic, so two bridges with the same Topic would kick each other from the broker (connect / disconnect ping-pong). Use unique Topics for each bridge.
