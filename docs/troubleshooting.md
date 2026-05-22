# Troubleshooting

Common issues and verified fixes, in rough order of frequency.

## AUTH_EXPIRE loop on ESP32-C3 Super Mini

**Symptom :** the serial monitor spams `[wifi] disconnected reason=2 (AUTH_EXPIRE)` once per second, never reaches `connected ip=...`. Same code works on a WROOM with the same credentials.

**Root cause :** the C3 Super Mini PA is miscalibrated above ~15 dBm on some boards. The saturated TX corrupts the WPA2 4-way handshake and the AP times out the auth state.

**Fix :** the firmware already calls `WiFi.setTxPower(WIFI_POWER_8_5dBm)` in `wifi_manager.cpp::init()`. No user action needed since v0.1.0.

If you still hit AUTH_EXPIRE despite the clamp :
1. Check the LED is on, board is powered (not a brown-out).
2. Verify the password (no typo, no leading space).
3. Use the 4-power-cycle recovery to re-enter the captive portal and double-check the credentials.

References : [arduino-esp32 issue #6767](https://github.com/espressif/arduino-esp32/issues/6767), [Arduino forum thread #1264358](https://forum.arduino.cc/t/esp32-c3-fails-to-connect-to-wifi-reason-2-auth-expired/1264358).

## CC1101 not responding

**Symptom :** boot log shows `[rf] cc1101 NOT responding (part=0x00 version=0x00)` or `part=0xFF version=0xFF`.

| Reading | Diagnosis |
|---|---|
| `part=0x00 version=0x00` | MISO held LOW. Soldering joint on MISO or VDD ; or chip not powered |
| `part=0xFF version=0xFF` | MISO floating. Chip not responding at all (likely dead) |
| `part=0x00 version=0x14` (or `0x04`, `0x07`) | Actually responding. Should not show NOT-responding ; if it does there is a firmware issue |

**Things to try :**

1. **Swap the CC1101 module** — clones have a 5-15 % failure rate. Try another one.
2. **Verify VCC** at the CC1101 module's VCC pin with a multimeter — must be ~3.3 V stable.
3. **Verify continuity** on the four SPI lines (SCK, MISO, MOSI, CSN) between the ESP32 GPIO and the CC1101 pin — < 1 Ω.
4. **Shorten the Dupont wires** to ≤ 5 cm. Long thin wires degrade SPI signals on borderline modules.
5. **Add 100 nF decoupling** across CC1101 VCC / GND, close to the chip.
6. **Add a 10 kΩ pull-up on MISO** to 3.3 V — fixes some clones whose MISO is too slow to settle.

**Note :** module / board affinity has been observed — the same CC1101 may work on one ESP32 and not another, even with identical wiring. Pair-match working combos rather than chase ghosts.

## mDNS not resolving

**Symptom :** `ping somfyrts2mqtt.local` returns "unknown host" but the bridge is on the LAN.

**Per OS :**

| OS | mDNS support |
|---|---|
| macOS / iOS | Built-in (Bonjour). Works out of the box |
| Windows 10 / 11 | Built-in (since Win10 1903). Works |
| Linux | Needs `avahi-daemon` — install with `sudo apt install avahi-daemon` then `sudo systemctl enable --now avahi-daemon` |
| Android | Some launchers / browsers do not resolve `.local`. Use the LAN IP shown in the bridge's Status card |

**Workaround :** the boot log prints the DHCP-assigned IP — use that directly (`http://192.168.0.75/`).

## Position drifts over time

**Symptom :** after several runs, the reported Position is off by 10-20 % from the visible shutter state.

**Why :** time-based estimation drifts due to motor wear, load variation (wind on awnings), and especially **external operation** (physical wall remote, another Somfy controller). The bridge has no way to observe those — Somfy RTS is unidirectional.

**Mitigation :**

1. The bridge auto-recalibrates on every **full Open** (snap Position to 100) and **full Close** (snap to 0). Do a full cycle now and then.
2. Manual resync via the UI : click the **🎯 Sync** button in the Setup row, enter the actual percentage, save. NVS updated, no RF.
3. Via MQTT : `cmnd/<root>/<name>/SetPosition <value>`.

## Open and Close inverted (awnings)

**Symptom :** `cmnd/.../Open` retracts the awning instead of extending it. `Close` extends.

**Root cause :** Somfy "store banne" motors are typically paired so that the Up button retracts (which feels like "close"). The default convention (`Up = Open = 100`) does not match the user mental model for awnings.

**Fix :** in the Remotes table, click the ⚙ gear on the row, tick **Invert Up/Down (awnings)** in the Setup sub-row. The bridge now swaps Up ↔ Down at the RF layer ; `Open` extends, `Close` retracts. Position stays in user space (100 = visually open).

## 4-power-cycle recovery does not trigger AP

**Symptom :** you cycle the power four times but the bridge keeps trying STA, never enters AP.

**Diagnosis :** the boot counter is reset between cycles because the bridge reaches `WL_CONNECTED` and stable uptime in less than 5 s between two cuts.

**Fix :** cut the power **before** the 5 s stable-uptime window. If the bridge is associating fast on your network, your cycle window is 0-5 s after each power-on. Keep cycles tight.

If the bridge is failing to connect (wrong creds), the counter never resets, so 4 cycles suffice no matter the timing.

To verify : watch the serial monitor for `[wifi] boot counter X/4` at each boot. If you see `boot counter reset (stable uptime reached)`, you waited too long.

## Erase command does not erase the remote

**Symptom :** click 🗑 Erase (7 s long press) on remote A, then 🔗 Prog brief on the same remote A → the remote stays paired.

**Root cause :** Somfy receivers **exclude the issuing remote** from the deletion candidates. You cannot self-erase from the same remote that issued the 7 s PROG.

**Fix :** the Erase workflow needs **two** paired remotes :

1. Click 🗑 Erase on remote A (SOURCE) — the 7 s long-press puts the motor in deletion mode.
2. Within 10 s of the motor jog, click 🔗 Prog briefly on remote B (TARGET) — remote B is now erased.

The web UI prompts a confirmation dialog with this workflow when you click Erase.

## Bridge keeps rebooting / brown-out

**Symptom :** boot log says `reset_reason=BROWNOUT` repeatedly.

**Root cause :** the 3.3 V LDO sags under load (WiFi TX bursts, MQTT TLS handshake, RF emission). Common on cheap C3 boards powered through a thin USB cable.

**Fix :**
1. Use a **5 V / 2 A wall adapter** (not a laptop USB port or a hub).
2. Switch to a **short, thick USB cable**.
3. Add a **100 µF electrolytic cap** + a **100 nF ceramic** across the C3's 3V3 / GND pins, close to the chip.

## How to read serial logs reliably

USB-CDC over the C3 Super Mini is sometimes flaky (drops bytes during high RX). Workarounds :

- Use `pio device monitor --raw` and grep for the relevant tag (`[wifi]`, `[rf]`, etc.).
- Power the bridge from a wall adapter (not the same USB you read logs on) — separates power supply and serial.
- If still unreliable, drop a temporary `MDNS.addService("debug", "tcp", 23)` + a Telnet server, and read logs over WiFi.
