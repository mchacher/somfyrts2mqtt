# Setup

End-to-end procedure from blank board to a working bridge on the LAN.

## Prerequisites

- PlatformIO Core (CLI) or PlatformIO IDE. Install with `pip install platformio` or via VSCode extension.
- A wired CC1101 (see [hardware.md](hardware.md)).
- A 2.4 GHz WiFi access point (WPA2-PSK recommended).
- A MQTT broker on the LAN (mosquitto, EMQX, HiveMQ, etc.). The broker can be set up later, the bridge tolerates `mqtt_connected=false` indefinitely.

## Build

No compile-time WiFi credentials needed. A fresh checkout builds straight into a captive-portal-enabled firmware.

```bash
git clone https://github.com/mchacher/somfyrts2mqtt.git
cd somfyrts2mqtt

# Optional : keep secrets.h empty (default) for AP-portal commissioning.
# OR pre-fill it for instant STA boot during dev (no AP step needed).
cp include/secrets.h.example include/secrets.h
$EDITOR include/secrets.h

# Build, type-check, run native tests
pio run
pio check
pio test -e native
```

## First flash

Connect the board over USB, then :

```bash
pio run -e esp32-c3-mini -t upload -t monitor   # for ESP32-C3 Super Mini
# or
pio run -e esp32-wroom   -t upload -t monitor   # for WROOM / NodeMCU-32S
```

The serial monitor should log :

```
[boot] hello somfyrts2mqtt v0.1.0 reset_reason=POWERON(1)
[nvs] ready schema=1 remotes=0
[wifi] entering AP mode (force=0, ssid_empty=1)
[wifi] captive portal SSID=somfyrts2mqtt-AB12CD timeout=300s
```

If you see `cc1101 NOT responding` instead, jump to [troubleshooting](troubleshooting.md#cc1101-not-responding).

## Captive portal commissioning

With NVS empty (fresh flash), the bridge boots into SoftAP mode.

1. On your phone or laptop, join the open WiFi **`somfyrts2mqtt-<MAC>`** (the suffix matches the chip ID — visible in the boot log).
2. Most modern OSes pop up the captive portal automatically. If not, open `http://192.168.4.1/` in any browser.
3. The portal (tzapu / WiFiManager) shows a SSID dropdown (scan refresh button) + a password field.
4. Pick your home SSID, enter the password, click Save.
5. The bridge reboots into STA mode and joins your LAN.

Boot log on a successful first STA connect :

```
[wifi] mDNS up : somfyrts2mqtt.local (http :80)
[wifi] connected ip=192.168.0.75 rssi=-67 channel=6 in 3120ms
```

Open `http://somfyrts2mqtt.local/` (or the LAN IP) and the admin web UI loads. See [web-ui.md](web-ui.md) for the section-by-section tour.

## MQTT broker configuration

The admin UI's `MQTT broker` card asks for :

- **Host** — broker IP or DNS name (e.g. `192.168.0.230`)
- **Port** — usually `1883` (or `8883` for TLS, not supported yet)
- **User** / **Pass** — optional, empty for anonymous brokers
- **Topic** — Tasmota-style root topic. Empty = default `somfyrts2mqtt`. Set a distinct value (e.g. `somfyrts2mqtt-rdc`) if you run multiple bridges on the same broker

Save the form and the bridge reconnects to the broker. The `MQTT` pill in the Status card flips to green when connected.

## Add a Somfy remote

Each Somfy receiver pairs with a 24-bit "remote ID" + rolling code. The bridge invents a new virtual remote that you pair with the motor once.

1. In the admin UI, scroll to the **Remotes** card.
2. Bottom row : enter a 6-hex ID of your choice (e.g. `A1B2C3`) and a MQTT-safe name (`kitchen_shutter`, `bedroom`, `store_terrasse` — only letters, digits, `_`, `-`).
3. Click **Add**. The remote appears in the table with rolling code 0.
4. Put the motor in pair mode (long-press PROG on an already-paired remote OR press the P2 button on the motor head).
5. In the bridge UI, click the gear icon next to your new remote, then click **🔗 Prog**. The motor jogs to confirm pairing.

You can now send Up / Stop / Down from the UI buttons, or via MQTT (see [mqtt-api.md](mqtt-api.md)).

## Calibration (for position tracking)

Position tracking is optional but useful. To calibrate :

1. Click the gear icon next to a remote in the Remotes table → the Setup row expands.
2. Time how long the motor takes to fully open the shutter (stopwatch).
3. Same for full close (durations can differ on some motors).
4. Enter the values in the **Calibration** fields. Click outside to commit (auto-save on blur).
5. For awnings ("store banne") tick the **Invert Up/Down** checkbox — the Open command now extends the awning instead of retracting it.

The bridge now reports Position (0-100 %) live during motion, both in the UI and in MQTT `tele/<root>/SENSOR`.

## Reconfigure WiFi (without re-flashing)

Three paths :

1. **Web UI** — Status card shows the SSID. The WiFi card has a SSID + Password form ; Save and reconnect. Use when the LAN is still working.
2. **Captive portal again** — visit the admin UI, factory-reset, the bridge reboots into the captive portal.
3. **4-power-cycle recovery** — for a bridge that you cannot reach (wrong WiFi creds, router changed, lost LAN access) :
   - Cycle power on the bridge 4 times in less than 5 seconds each
   - On the 4th boot, the boot counter reaches the AP threshold and the bridge enters the captive portal automatically
   - Same workflow as first-time commissioning

The counter resets after 5 s of stable WiFi association ; if WiFi is down, it never resets — exactly the case the recovery is meant to fix.

## Factory reset

Two ways :

- **Web UI** — Danger zone → Factory reset → confirm twice. NVS is wiped and the bridge reboots into AP mode.
- **`pio run -t erase`** — wipes the entire flash (NVS + app). Re-upload firmware after.

NVS is wiped, MQTT broker / remotes / WiFi credentials all lost. The bridge starts fresh in captive portal mode.
