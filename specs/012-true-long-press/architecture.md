# Architecture 012

## Touched modules

| File | Change |
|---|---|
| `src/rf.cpp` | Add `static build_frame()`, `static send_frame()`, helpers, and public `rf::send_somfy_longpress()`. `rf::send_somfy()` unchanged |
| `include/rf.h` | Add `send_somfy_longpress()` prototype next to the existing `send_somfy()` |
| `src/orchestrator.cpp` | `handle_command()` dispatches to `rf::send_somfy_longpress()` when `long_press == true` |
| `include/orchestrator.h` | `handle_command()` and `enqueue_command()` gain a `bool long_press = false` argument |
| `src/web_ui.cpp` | Set `long_press=true` when the URL matches `program3s` / `program7s` |
| `src/orchestrator.cpp` queue | Store the flag in `QueuedCommand` |
| `test/test_rts_frame/` | New native suite — frame builder equivalence vs Legion2 lib (known vector captured) |

`platformio.ini` is **not** edited — `Legion2/Somfy_Remote_Lib` stays as a dep for the short path.

## Dispatch (where the flag flows)

```
Web UI button "🗑 Erase"
  └─ POST /api/remotes/<id>/program7s
       └─ web_ui handler maps program7s → {cmd=Program, repeat=50, long_press=true}
            └─ orchestrator::enqueue_command(id, cmd, 50, long_press=true)
                 └─ FIFO queue
                      └─ orchestrator::process_queue() drains
                           └─ orchestrator::handle_command(id, cmd, 50, true)
                                ├─ NVS rolling code increment + persist
                                └─ rf::send_somfy_longpress(id, code, 0x08, 50)
                                     ├─ build_frame() — same byte layout as the lib
                                     └─ tight loop: 1 initial frame + 50 repeats
                                              with INTER_FRAME_GAP_US = 3 000 µs

Web UI button "▲ Up"  (or MQTT topic .../set up)
  └─ orchestrator::handle_command(id, Up, /*repeat_override*/ -1, /*long_press*/ false)
       └─ rf::send_somfy(id, code, 0x02, /*repeat*/ 1)
            └─ Legion2 lib path (unchanged since iter 006)
```

## Inline Somfy RTS frame builder

Frame layout matches `Legion2/Somfy_Remote_Lib::buildFrame()` exactly — verified by a native test against a captured vector.

```cpp
// Reference: Pushstack Somfy RTS reverse-engineering
//   https://pushstack.wordpress.com/somfy-rts-protocol/
static void build_frame(uint8_t button,
                        uint16_t rolling_code,
                        uint32_t remote_id,
                        uint8_t out[7]) {
  out[0] = 0xA7;
  out[1] = static_cast<uint8_t>(button << 4);   // lower nibble = checksum slot
  out[2] = static_cast<uint8_t>(rolling_code >> 8);
  out[3] = static_cast<uint8_t>(rolling_code & 0xFF);
  out[4] = static_cast<uint8_t>((remote_id >> 16) & 0xFF);
  out[5] = static_cast<uint8_t>((remote_id >>  8) & 0xFF);
  out[6] = static_cast<uint8_t>( remote_id        & 0xFF);

  uint8_t cks = 0;
  for (int i = 0; i < 7; ++i) cks ^= out[i] ^ (out[i] >> 4);
  cks &= 0x0F;
  out[1] |= cks;

  for (int i = 1; i < 7; ++i) out[i] ^= out[i - 1];
}
```

## Emission timing (constants in `rf.cpp`)

| Constant | Value | Source |
|---|---|---|
| `SYMBOL_US` | 640 | Half manchester period |
| `WAKEUP_HIGH_US` | 9 415 | Matches the lib |
| `WAKEUP_LOW_US` | 9 565 | Matches the lib |
| `WAKEUP_SILENCE_MS` | 80 | Silence between wakeup and first frame |
| `HW_SYNC_FIRST` | 2 | 2 hw-sync pulses on the first frame |
| `HW_SYNC_REPEAT` | 7 | 7 hw-sync pulses on subsequent frames |
| `SW_SYNC_HIGH_US` | 4 550 | Software sync high |
| `INTER_FRAME_SILENCE_US` | 415 | End-of-frame silence |
| `INTER_FRAME_GAP_US` | **3 000** | **CHANGED vs lib's 30 000** — tight enough that the data-to-data gap stays under the motor's release threshold |

`INTER_FRAME_GAP_US` is the **only** functional difference vs the lib: 3 ms instead of 30 ms. Combined with the 7 hw-sync pulses (~35.8 ms), the data-to-data gap is ~45 ms, where the motor reliably sees a continuous press.

## send_somfy_longpress flow

```
rf::send_somfy_longpress(remote_id, rolling_code, button, repeat)
  if (!s_ready) return false
  build_frame(button, rolling_code, remote_id, frame)
  send_frame_longpress(frame, HW_SYNC_FIRST, /*wakeup*/ true)
  for i in 0..repeat-1
    delayMicroseconds(INTER_FRAME_GAP_US)
    send_frame_longpress(frame, HW_SYNC_REPEAT, /*wakeup*/ false)
  log "tx-lp id=... code=... button=0x.. repeat=..."
  return true
```

`send_frame_longpress()` runs entirely under `noInterrupts()`. On the C3 single-core, WiFi events stall during the ~110 ms bit-bang window per frame, with 3 ms gaps to breathe — that is a tighter cycle than iter 006's 50 ms bit-bang + 30 ms gap, but the total emission window is ~7 s and we already showed in iter 011 that WiFi tolerates it.

## Total emission times with the new path

For `repeat=50` (Erase 7 s) — should match user-perceived "7 s" press:
- Initial frame: wakeup (19 ms) + 80 ms silence + 2 hw sync (10.2 ms) + sw sync (5.2 ms) + 56 bits × 2 × 640 µs (71.7 ms) + 415 µs silence = **~186 ms**
- Each repeat: 3 ms gap + 7 hw sync (35.8 ms) + sw sync (5.2 ms) + 71.7 ms data + 415 µs silence = **~116 ms**
- Total: 186 + 50 × 116 = **~5 986 ms ≈ 6 s**

To still match a 7 s user-perceived press, bump the Erase repeat to **60** (60 × 116 + 186 = ~7.15 s). Same logic for Pair 3 s: bump from 21 to **25** (25 × 116 + 186 = ~3.1 s). Update both the orchestrator-side defaults and the web UI's `TX_MS` lookup.

## What stays from iter 011

- Async queue (drained 1 per tick), with the `QueuedCommand` struct extended with `bool long_press`.
- The web UI's `TX_MS` lookup (values bumped to the new timings).
- The PROG / Pair / Erase visual distinction (colors, icons).
- The WiFi sticky-bad-BSSID recovery.
- The short-command path through `rf::send_somfy()` and the Legion2 lib (untouched).
