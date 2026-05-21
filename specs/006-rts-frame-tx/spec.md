# 006 — first real Somfy RTS frame

## Goal
Replace the stubbed `rf::send_somfy()` (iter 004) with real Somfy RTS emission over the CC1101 at 433.42 MHz. After this iteration, an MQTT command goes end-to-end: `mosquitto_pub` → orchestrator → CC1101 → motor moves.

The bridge becomes functionally complete: configure remotes via the web UI, pair the bridge as a virtual remote with an existing Somfy receiver, then control the shutters from any MQTT client.

## Scope

**In scope:**
- `rf::init()`: configure the CC1101 for async OOK at 433.42 MHz, set PA to +10 dBm, enter TX mode. Pre-condition for any emission.
- `rf::send_somfy(remote_id, rolling_code, button)`: build a real Somfy RTS frame via `Legion2/Somfy_Remote_Lib` and emit it on `CC1101_GDO0`. The orchestrator-managed rolling code is passed explicitly via `sendCommandWithCode()` — the lib's internal `RollingCodeStorage` is **not** used (we own persistence via `nvs_store`).
- Bug fix: `orchestrator::command_to_button` returned `0x80` for `Program`; the Somfy RTS button byte is `0x08`. Without this fix, the pairing PROG frame would have been malformed and ignored by the motor. Native test updated accordingly.

**Out of scope:**
- RX / sniffing existing remotes (`CC1101_GDO2` stays unwired).
- Asynchronous emission (we block the loop for ~50 ms per send -- acceptable, Somfy commands are at human cadence).
- Power tuning per remote (PA=+10 dBm is enough for whole-house range with a 17.3 cm antenna ; iter 012 if needed).
- Multiple-receiver groups (one virtual remote = one stored id ; mapping a stored id to several physical motors is the Sowel side's job).

## Acceptance criteria
- [ ] Build clean (zero warnings) on `esp32-c3-mini` and `esp32-wroom`.
- [ ] `pio check` zero defects on both.
- [ ] `pio test -e native` all green ; `command_to_button` test updated to expect `0x08` for `Program`.
- [ ] On a board with CC1101 + antenna, boot logs the CC1101 ping (`part=0x00 version=0x14`) and the OOK configuration (`mod=2 pa=10 pkt_format=3`).
- [ ] HW pairing test passes (procedure in `plan.md`).
- [ ] HW control test passes (UP/DOWN/STOP move the shutter that was paired).
- [ ] CI green on the PR.

## Decisions
- **`Legion2/Somfy_Remote_Lib`** — already in `lib_deps`, maintained fork, exposes `sendCommandWithCode()` which accepts an external rolling code. The internal `RollingCodeStorage` interface is **not** used: we keep the single source of truth in `nvs_store` via the orchestrator's persist-before-emit pattern.
- **Async OOK mode (`pkt_format=3`, `sync_mode=0`)** on the CC1101 — the carrier is gated directly by the level on GDO0, so the lib's bit-banging (`digitalWrite` + `delayMicroseconds`) modulates RF without any encoding side effects.
- **`+10 dBm` PA** — Somfy remotes are usually around +9 dBm. +10 dBm gives us a tiny edge and is safe ; the CC1101 max is +12 dBm. Higher TX powers heat the chip and drain more current.
- **Emit on GDO0 (= GPIO10)** — matches the iter 008 pinout. The lib doesn't care about the underlying RF chip; it just toggles whatever GPIO we pass to the `SomfyRemote` constructor.
- **No interrupt protection from the lib's `noInterrupts()`** — the lib already does this around the timing-critical bits ; we trust it. ESP32 runs WiFi on a separate task so the brief `noInterrupts()` window (~50 ms) doesn't drop MQTT messages.
- **Default 4 retries** per `sendCommand` — Somfy receivers normally accept the first ; the lib's 4-retry default is for robust reception in a noisy environment. We keep it.
