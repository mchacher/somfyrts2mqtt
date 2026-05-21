# Plan 012

## Steps

1. **Capture a known-good frame vector.** Flash the current iter 011 firmware, send a UP command with a fixed id `0xA1B2C3` and rolling code `0x1234`, and capture the obfuscated 7 bytes from a serial debug log (or read them from the lib's `printFrame()` after enabling `DEBUG`). Store the result as a `constexpr uint8_t LIB_FRAME_VECTOR[7]` in the new test file. This pins down the lib's exact byte layout for the equivalence test.
2. **Add the inline frame builder to `src/rf.cpp`.** Static `build_frame()` per architecture. No dependency on the lib here.
3. **Add `send_frame_longpress()`** with the new constants and `noInterrupts()` window. Initial-frame variant handles the wakeup pulse + 80 ms silence; repeat-frame variant just does 7 hw-sync + data + silence.
4. **Add public `rf::send_somfy_longpress()`** in `src/rf.cpp` and its prototype in `include/rf.h`. Same signature shape as `send_somfy()`.
5. **Plumb the `long_press` flag through the orchestrator:**
   - `include/orchestrator.h`: add `bool long_press = false` to `handle_command()` and `enqueue_command()`. Default false keeps existing callers (MQTT path) compatible.
   - `src/orchestrator.cpp`: extend `QueuedCommand` with the flag; `handle_command()` dispatches to `rf::send_somfy_longpress()` when true, else falls through to the existing `rf::send_somfy()` call.
6. **Update `src/web_ui.cpp`** to set `long_press=true` when `cmd_param` matches `program3s` or `program7s`. Bump the repeat overrides to **25** (Pair) and **60** (Erase) to compensate for the tighter inter-frame gap. Update the `TX_MS` JS lookup: `program3s: 3200, program7s: 7300`.
7. **Add native tests** in a new directory `test/test_rts_frame/test_main.cpp`:
   - `build_frame_known_vector` — `build_frame(button=Up, code=0x1234, id=0xA1B2C3)` matches `LIB_FRAME_VECTOR` byte-for-byte
   - `build_frame_button_byte_after_obfuscation` — XOR cascade preserved (build then de-obfuscate to recover the plain layout)
   - `build_frame_checksum_in_low_nibble_of_frame1` — checksum lands in the lower 4 bits of `frame[1]` before obfuscation
   - `build_frame_program_button_is_0x08` — regression vs the historical 0x80 bug
8. **`pio run` + `pio check` + `pio test -e native`** both envs. All green.
9. **HW flash + validate** per the test matrix below.
10. **PR + CI green + merge.**

## Test plan

### Native (Unity)

| Test | Scenario |
|---|---|
| `build_frame_known_vector` | Fixed input → exact byte array matches captured lib output |
| `build_frame_button_byte_after_obfuscation` | De-obfuscate the built frame, verify upper nibble of `frame[1]` = `button` |
| `build_frame_checksum_in_low_nibble_of_frame1` | Lower nibble of `frame[1]` (pre-obfuscation) = XOR of all nibbles |
| `build_frame_program_button_is_0x08` | `button=0x08` (Program) → upper nibble of `frame[1]` is `0x8`, not `0x0` |

### HW (browser session)

Preconditions: C3 + CC1101 + SMA antenna; one Somfy remote `A1B2C3` already paired with a real shutter (from iter 006); one fresh id `D1D2D3` configured in the UI, NOT yet paired with the motor.

| Case | Action | Expected |
|---|---|---|
| **Regression Up** | Click ▲ on `A1B2C3` | Shutter goes up. Rolling code +1. Lib path unchanged. |
| **Regression Stop** | Click ■ during travel | Shutter stops. |
| **Regression Down** | Click ▼ on `A1B2C3` | Shutter goes down. |
| **Regression Prog brief** | Click 🔗 Prog on `A1B2C3` | Motor in pair-pending if the motor was waiting; otherwise no visible effect. |
| **Pair via UI (new path)** | Click ➕ Pair on `A1B2C3`, motor must jog within ~3 s. Within 10 s of the jog, click 🔗 Prog on `D1D2D3` | Motor jogs after the 3 s. Then jogs again after brief Prog → `D1D2D3` paired. |
| **Erase via UI (new path)** | Click 🗑 Erase on `A1B2C3`, motor must jog at ~7 s. Within 10 s of the jog, click 🔗 Prog on `D1D2D3` | Motor jogs once at 7 s. After brief Prog, `D1D2D3` is erased. Up/Down from `D1D2D3` no longer move the shutter. |
| **No shutter motion during Erase** | Click 🗑 Erase, watch the shutter during the 7 s | Shutter stays still (the iter 011 symptom must be gone). |
| **MQTT short path still works** | `mosquitto_pub -t somfy2mqtt/A1B2C3/set -m up` | Shutter goes up via the lib path (MQTT does not pass `long_press=true`). |
| **Queue interleaving** | Click 🗑 Erase, then immediately `mosquitto_pub` an Up to another remote | Both emissions execute FIFO from the queue. Logs show `tx-lp` then `tx` (or vice-versa depending on click order). |

## Risks

- **Tight inter-frame gap (3 ms) may starve WiFi during the 7 s emission window.** Iter 011 already proved the C3 tolerates 7 s of mostly-blocked `loop()`; the 3 ms gap is still room for WiFi events between `noInterrupts()` windows. If WiFi flaps reappear, bump the gap to 10 ms and re-test (still well under the lib's 30 ms).
- **The captured lib vector must be byte-stable.** If a future version of Legion2 changes the obfuscation, the native test fails fast and forces a refresh. That is the desired behavior.
- **Two code paths to keep in sync.** Mitigated by the equivalence test on the data layer — only the timing layer diverges by design.
