# Plan 005

## Steps

1. Replace the stub body of `rf::init()` in `src/rf.cpp` with the real sequence: `setSpiPin` → read `PARTNUM` + `VERSION` → validate → `Init` + `setMHZ` → log result.
2. Keep `rf::send_somfy()` as the stub for now — iter 006 will swap it.
3. Run `pio run` (zero warnings), `pio check` (zero defects), `pio test -e native` (all green).
4. Flash and observe the boot log.

## Test plan

### Native (Unity)

No new native tests — the change is entirely HW-coupled. The 31 existing tests (5 orchestrator + 12 mqtt + 13 nvs + smoke) still cover the rest.

### HW (manual, requires the CC1101 module wired per CLAUDE.md)

| Case | Action | Expected on serial |
|---|---|---|
| **CC1101 wired correctly** | Flash + boot | `[rf] cc1101 ok part=0x00 version=0x14 freq=433.42` (or `version=0x04`/`0x07` on clones) |
| **CC1101 unplugged** | Pull VCC or any SPI pin then flash + boot | `[rf] cc1101 NOT responding (part=0xFF version=0xFF) -- check wiring/power`. WiFi / MQTT / web UI still come up. |
| **End-to-end chain still works** | `mosquitto_pub -t somfy2mqtt/A1B2C3/set -m up` after a good boot | Same orchestrator chain as iter 004 (rolling code increments, state retained), `[rf] STUB tx ...` still logged because emission is iter 006. |
