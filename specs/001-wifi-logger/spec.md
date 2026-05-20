# 001 — wifi + logger

## Goal
Lay down the firmware's modular skeleton with two foundation bricks: a **tag-prefixed serial logger** and a **non-blocking WiFi manager with automatic reconnect**. No business logic — just validate the code structure and demonstrate network connectivity.

## Scope

**In scope:**
- `logger` module (namespace `logger`): `Serial.printf` wrapper with `[<tag>]` prefix
- `wifi` module (namespace `wifi`): connect + auto-reconnect + state (connected / connecting / disconnected)
- Boot sequence in `main.cpp`: `setup()` initialises the logger then WiFi
- WiFi credentials via `include/secrets.h` (gitignored), template `include/secrets.h.example` committed

**Out of scope:**
- Web UI / runtime provisioning (iter 007)
- mDNS, custom hostname, IPv6
- Watchdog / auto-restart
- File or network log sinks (Serial only)
- Filterable log levels (info/warn/err exposed, but no dynamic filter)

## Acceptance criteria
- [ ] `include/secrets.h.example` committed; `include/secrets.h` listed in `.gitignore`
- [ ] On boot the serial console prints, in order: `[boot] hello somfyrts2mqtt`, `[wifi] connecting ssid=<x>`, `[wifi] connected ip=<x.x.x.x>`, within 10 seconds
- [ ] If WiFi is dropped (router off), the console prints `[wifi] disconnected`, then `[wifi] connected ip=<x.x.x.x>` when WiFi comes back, with no manual intervention
- [ ] API `logger::info(tag, fmt, ...)`, `logger::warn(tag, fmt, ...)`, `logger::err(tag, fmt, ...)` available
- [ ] Build is clean (zero warnings, zero errors), flashes, and runs on the ESP32-C3 Super Mini

## Decisions
- Selected: **WiFi credentials in `secrets.h` (gitignored)**. Rejected alternative: NVS provisioning over serial at first boot (the web UI in iter 007 covers all runtime config anyway; no point building a transient serial provisioning step).
- Selected namespace name: **`logger`** rather than `log`. Avoids any clash with `log()` from `<cmath>`, which `Arduino.h` pulls in transitively.
