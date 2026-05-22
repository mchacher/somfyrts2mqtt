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

State (bridge publishes)
  tele/<root>/LWT                        "Online" | "Offline"   (retained + LWT)
  tele/<root>/SENSOR                     JSON, every 1 s during motion + on state change
  stat/<root>/<name>                     JSON, ack after each cmnd
```

The bridge subscribes with a single wildcard `cmnd/<root>/+/+`, so adding a new remote is a NVS operation only — no re-subscribe needed.

## Payloads

### `tele/<root>/SENSOR` (aggregated, 1 Hz during motion)

```json
{
  "kitchen": {"Position": 45, "Direction": 1, "Target": 100},
  "bedroom": {"Position": 0,  "Direction": 0, "Target": 0}
}
```

| Field | Range | Meaning |
|---|---|---|
| `Position` | 0..100 | Current estimated position. 0 = closed, 100 = open (or extended for awnings) |
| `Direction` | -1, 0, 1 | -1 closing, 0 idle, 1 opening (Tasmota convention) |
| `Target` | 0..100 | Destination of the current or last motion |

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
