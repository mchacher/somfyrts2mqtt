# 004 — rf (stub) + orchestrator

## Goal
Wire the full business-logic chain. An MQTT command `somfy2mqtt/<id>/set` triggers:
- a lookup of the remote in NVS,
- the **persistent** increment of its rolling code,
- a call to `rf::send_somfy()` (stubbed for iter 004; the real CC1101 emission lands in iter 006),
- the retained publish of the new state and rolling code.

At the end of this iteration the firmware is **functionally complete** on the business-logic side. Iter 005 / 006 will swap the RF stub for real CC1101 emission without touching the rest.

## Scope

**In scope:**
- Namespace `rf` with `init()` and `send_somfy(id, code, button)` — both stubbed (logs only).
- Namespace `orchestrator` exposing `handle_command(remote_id, mqtt::Command)`. Wires the chain: NVS lookup → increment rolling code → persist → `rf::send_somfy` → publish state + rolling_code retained.
- Translation `mqtt::Command` → Somfy button code (`Up=0x02`, `Down=0x04`, `Stop/My=0x01`, `Program=0x80`).
- **Persistence-before-emission**: `update_rolling_code` is committed to NVS **before** the RF call, to avoid replaying an old code if the board reboots mid-emission.
- Native unit tests for `command_to_button` mapping.
- `main.cpp` swap: replace the stub command handler with `orchestrator::handle_command`; call `rf::init()` once at boot.

**Out of scope:**
- Real CC1101 init / SPI / emission (iter 005, 006).
- Per-remote retry on `rf::send_somfy` failure.
- Rate limiting / debouncing of repeated commands.
- "Seed test remote" fixture — the web UI from iter 007 already handles remote management; the user adds the test remote interactively.

## Acceptance criteria
- [ ] Build clean (zero warnings), `pio check` zero defects, `pio test -e native` all green.
- [ ] `mosquitto_pub -t somfy2mqtt/A1B2C3/set -m up` produces this serial sequence (assuming `A1B2C3` was added via the web UI beforehand):
  ```
  [mqtt] cmd id=A1B2C3 cmd=up
  [rf]   STUB tx id=A1B2C3 code=<prev+1> button=0x02
  [orch] executed up on A1B2C3 code=<prev+1>
  ```
- [ ] After the command, the broker shows `somfy2mqtt/A1B2C3/state = up` (retained) and `somfy2mqtt/A1B2C3/rolling_code = <prev+1>` (retained).
- [ ] On reboot, the rolling code persists (next emission uses `<prev+2>`, not `<prev+1>`).
- [ ] Sending to an unknown id (e.g. `somfy2mqtt/FFFFFF/set`) logs `[orch] unknown remote FFFFFF, drop` and publishes nothing.

## Decisions
- **Persist BEFORE emit**: a Somfy receiver rejects rolling codes ≤ the last accepted one, so if we replay a code after a reboot the motor ignores us. Committing the increment first guarantees we never replay; the only failure mode is "we incremented but missed an emission", which is harmless (motor never sees a duplicate).
- **`orchestrator` is its own namespace** rather than a function in `main.cpp` so future trigger paths (web UI command buttons, scheduled actions) can wire into the same chain.
- **`rf::send_somfy(id, code, button)` signature is the final one** — iter 006 will swap the implementation without changing callers.
- **No retry on RF failure**: stub never fails, and once the real RF lands, retries would re-increment the rolling code, defeating replay protection. RTS is unidirectional anyway — the next user command will increment further.
- **Remote management lives in the web UI (iter 007)**, no longer in firmware seed code. This iter assumes the user adds remotes via `http://<board>/`.
