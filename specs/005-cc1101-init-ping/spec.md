# 005 — cc1101 init + ping

## Goal
Replace the iter 004 stub of `rf::init()` with a real CC1101 setup: configure SPI pins, verify the chip responds by reading the `PARTNUM` and `VERSION` status registers, and calibrate the carrier frequency to 433.42 MHz. No frame emission — that lands in iter 006. This iter is the wiring sanity check.

## Scope

**In scope:**
- `rf::init()` configures SPI via `ELECHOUSE_cc1101.setSpiPin(SCK, MISO, MOSI, CSN)`.
- Reads `PARTNUM` (status register `0x30`, expected `0x00`) and `VERSION` (status register `0x31`, expected `0x14` for standard batches; `0x04`/`0x07` for some clones).
- On success: calls `Init()`, sets the carrier frequency to `SOMFY_FREQ_MHZ` (433.42 MHz), logs `[rf] cc1101 ok part=0x.. version=0x.. freq=433.42`.
- On failure: logs an error with both raw values and a "check wiring" hint; the rest of the firmware keeps running (web UI + MQTT still useful for non-RF dev).
- `rf::send_somfy()` stays the stub from iter 004 — iter 006 swaps it.

**Out of scope:**
- Frame emission (iter 006).
- RX configuration / sniffing existing remotes (later, if needed).
- Power level tuning / antenna calibration measurements.
- Quartz-mismatch (27 MHz vs 26 MHz) auto-detection — we trust the 26 MHz on the board (verified visually on the crystal).

## Acceptance criteria
- [ ] Build clean (zero warnings), `pio check` zero defects, `pio test -e native` all green.
- [ ] On boot, serial shows `[rf] cc1101 ok part=0x00 version=0x14 freq=433.42` (version may differ on cloned modules — `0x04` or `0x07` are also acceptable).
- [ ] If the module is unplugged, the firmware logs `[rf] cc1101 NOT responding (part=0xFF version=0xFF) — check wiring/power` and keeps booting (WiFi + MQTT + web UI still work).
- [ ] Existing `orchestrator::handle_command` chain still functions end-to-end on a connected board (the stub `send_somfy` is unaffected).

## Decisions
- **Use `ELECHOUSE_cc1101` from SmartRC-CC1101-Driver-Lib** — already in `lib_deps`, well-known in the ESP32 community, exposes `setSpiPin()` and `SpiReadStatus()` which are exactly what we need.
- **Read raw status registers directly** for the ping rather than rely on `getCC1101()` alone, so we can log the actual bytes — useful diagnostics when something is half-wired.
- **Non-fatal init failure**: if the CC1101 does not respond we degrade gracefully (no RF, but the rest of the firmware stays up). The orchestrator already accepts `rf::send_somfy()` returning false.
- **Set the frequency once in `init()`** rather than on every emit. CC1101 frequency configuration is stateful; setting it during emission would add latency.
