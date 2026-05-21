# 012 — true long-press for PROG (selective inline emission)

## Goal
Make the PROG long-press variants (Pair 3 s, Erase 7 s) of iter 011 actually trigger Somfy's pair / erase modes on the motor. After this iteration, deleting a remote from a motor (or putting it in pair mode) is fully doable from the web UI, with no physical button on a paired remote required.

## Background
Iter 011 ships PROG 3 s / 7 s buttons by passing `repeat=21` / `repeat=50` through `Legion2/Somfy_Remote_Lib::sendCommandWithCode()`. On HW validation, the motor moves the shutter during the 7 s emission instead of entering erase mode, and the target remote stays paired afterwards.

Root cause: the lib's `sendFrame()` inserts `delay(30)` between frames (see `.pio/libdeps/.../SomfyRemote.cpp`) and starts each repeat frame with seven hardware-sync pulses (35.8 ms). The data-to-data gap is ~71 ms, which some Somfy receivers interpret as button release. The motor then treats every frame as a separate brief PROG press, cycling through pair-pending / confirm states and jogging the shutter at each transition.

Real Somfy remotes keep the data-to-data gap tighter and the motor's long-press detection requires that. The Legion2 lib trades long-press accuracy for inter-frame breathing room (WiFi / interrupts).

## Scope

**Selective replacement** — Up / Stop / Down / Prog brief stay on the proven `Legion2/Somfy_Remote_Lib::sendCommandWithCode()` path (no regression risk on what works today since iter 006). Only the long-press variants (Pair 3 s, Erase 7 s) take the new inline path with tight inter-frame timing.

**In scope:**
- New internal function `rf::send_somfy_longpress(remote_id, rolling_code, button, repeat)` in `src/rf.cpp` that inlines the Somfy RTS frame builder + bit-bang emitter with a tight inter-frame gap (~3 ms instead of the lib's 30 ms).
- Plumbing: a `bool long_press` flag added to `orchestrator::handle_command()` and `orchestrator::enqueue_command()`. The web UI sets it true for `program3s` / `program7s`. MQTT path always passes false.
- Native unit tests for the inline frame builder: button-byte placement, rolling-code byte order, XOR checksum, XOR cascade obfuscation, and one end-to-end byte-for-byte equivalence test against a known-good frame captured from the Legion2 lib (vector stored as a constant in the test).
- The existing `rf::send_somfy()` signature and behavior are untouched. `Legion2/Somfy_Remote_Lib` stays in `lib_deps`.

**Out of scope:**
- Refactoring the short-command path. It works since iter 006, do not touch.
- Replacing the CC1101 radio driver.
- RX / sniffing.

## Acceptance criteria
- [ ] Build clean on `esp32-c3-mini` and `esp32-wroom`.
- [ ] `pio check` zero defects on both envs.
- [ ] `pio test -e native` all green; new test suite `test_rts_frame` covers `build_frame` byte layout + checksum + obfuscation + known vector.
- [ ] HW regression: brief Up / Stop / Down / Prog still work (proves the short path was not perturbed).
- [ ] HW: Pair 3 s on a paired remote → motor jogs within ~3 s → brief Prog on a new remote pairs it.
- [ ] HW: Erase 7 s on a paired remote → motor jogs at ~7 s → brief Prog on the target remote erases it (target remote can no longer move the shutter).
- [ ] Shutter must NOT move during the 7 s of Erase emission (the iter 011 symptom).
- [ ] CI green on the PR.

## Decisions
- **Selective replacement, not full.** The lib stays the canonical path for short commands. Only long-press variants take the new path. Rationale: minimum surface area of change, zero regression risk on the proven short path. Cost: ~80 lines of duplicated frame-building code, locked down by a byte-for-byte test against a captured lib vector.
- **Dispatch via an explicit `long_press` flag**, not heuristics on `repeat` values. The caller (web UI) knows whether the user clicked Pair / Erase; pass that intent down rather than reverse-engineering it.
- **Same public API for `rf::send_somfy()`.** No change to its signature. Adding a flag would force every caller to specify it; instead we add a new function.
- **Inter-frame gap = 3 ms** in the new path (vs lib's 30 ms). Pushstack-style tight timing for proper long-press detection. Documented inline with a citation.
- **`noInterrupts()` around bit-bang** kept (same as lib). Bit timing is microsecond-sensitive.
- **No fallback.** Once shipped, the long-press path is the only path for long-press. No toggle.
