# Architecture 005

## Touched modules

| File | Role |
|---|---|
| `src/rf.cpp` | Replace the iter 004 stub `init()` with real CC1101 setup; `send_somfy()` stays a stub |

No public API change (`rf.h` is untouched), no `platformio.ini` change (`SmartRC-CC1101-Driver-Lib` and the SPI pin constants from `config.h` are already in place).

## CC1101 status registers used

| Reg | Addr | Expected | Notes |
|---|---|---|---|
| `PARTNUM` | `0x30` | `0x00` | Identifies the chip family (always 0 for CC1101) |
| `VERSION` | `0x31` | `0x14` | Standard die; clones may report `0x04` or `0x07`. We accept any non-`0x00`/`0xFF`. |

A `0xFF` (or `0x00` on both registers) read with the SS strobed indicates no SPI response — wiring or power problem.

## Flow

```
rf::init()
  ├─ ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CSN)
  ├─ part    = ELECHOUSE_cc1101.SpiReadStatus(0x30)
  ├─ version = ELECHOUSE_cc1101.SpiReadStatus(0x31)
  ├─ ok = (part == 0x00) && (version != 0x00) && (version != 0xFF)
  ├─ if (!ok):
  │     logger::err("rf", "cc1101 NOT responding (part=0x%02X version=0x%02X) -- check wiring/power", part, version)
  │     s_ready = false
  │     return false
  ├─ ELECHOUSE_cc1101.Init()
  ├─ ELECHOUSE_cc1101.setMHZ(SOMFY_FREQ_MHZ)
  ├─ s_ready = true
  └─ logger::info("rf", "cc1101 ok part=0x%02X version=0x%02X freq=%.2f", part, version, SOMFY_FREQ_MHZ)

rf::send_somfy(...)
  └─ unchanged stub for iter 005 (logs and returns true)
```

## Failure handling

- If `init()` returns false, `s_ready` stays false; `send_somfy()` returns false; the orchestrator catches that and logs `rf::send_somfy failed`. The firmware keeps running so the web UI is still reachable to inspect remotes, edit MQTT, etc.
