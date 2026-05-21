# Architecture 006

## Touched modules

| File | Change |
|---|---|
| `src/rf.cpp` | `init()` adds OOK config + TX mode ; `send_somfy()` replaces the stub log with real emission via `SomfyRemote::sendCommandWithCode()` |
| `include/orchestrator.h` | Bug fix: `command_to_button(Program)` returns `0x08` instead of `0x80` |
| `test/test_orchestrator/test_main.cpp` | Updated assertion for `Program` button |

No `nvs_store` change (rolling code persistence already in place since iter 002), no `mqtt` change, no `wifi_manager` change.

## CC1101 setup for async OOK

In `rf::init()`, after the ping succeeds:

```cpp
ELECHOUSE_cc1101.setMHZ(SOMFY_FREQ_MHZ);   // 433.42 MHz carrier
ELECHOUSE_cc1101.setModulation(2);          // ASK / OOK
ELECHOUSE_cc1101.setPA(10);                 // +10 dBm
ELECHOUSE_cc1101.setSyncMode(0);            // no preamble / sync word
ELECHOUSE_cc1101.setPktFormat(3);           // PKT_FORMAT=3 -- async serial mode
ELECHOUSE_cc1101.SetTx();                   // enter TX
pinMode(CC1101_GDO0, OUTPUT);               // we drive the modulation input
digitalWrite(CC1101_GDO0, LOW);
```

In async serial mode, the CC1101 keys the carrier directly from the GDO0 input level. The lib's `digitalWrite()` toggling on GDO0 thus modulates the RF.

## Emission flow

```
orchestrator::handle_command(remote_id, mqtt::Command::Up)
  ├─ nvs_store::get_remote(remote_id, ...)
  ├─ next_code = remote.rolling_code + 1
  ├─ nvs_store::update_rolling_code(remote_id, next_code)   ← persist BEFORE emit
  ├─ button = command_to_button(mqtt::Command::Up)          ← 0x02
  └─ rf::send_somfy(remote_id, next_code, button)
       ├─ SomfyRemote remote(CC1101_GDO0, remote_id, nullptr);
       ├─ remote.setup();
       └─ remote.sendCommandWithCode(static_cast<::Command>(button), next_code, 4);
             ├─ buildFrame(...)                              ← key, button, code, id, checksum, obfuscation
             ├─ sendFrame(sync=2)                            ← wake-up + 2-sync first frame
             └─ for (i=0; i<4; ++i) sendFrame(sync=7)        ← 4 repeats with 7-sync
```

The lib bit-bangs on `CC1101_GDO0` (= GPIO10) with `digitalWrite()` + `delayMicroseconds()` inside `noInterrupts()` blocks. Total emission time ~50 ms.

## Button mapping (corrected)

| `mqtt::Command` | Somfy button | Byte value |
|---|---|---|
| `Up` | UP | `0x02` |
| `Down` | DOWN | `0x04` |
| `Stop` | MY (centre / stop) | `0x01` |
| `Program` | PROG | **`0x08`** (was `0x80` — bug) |
| `Invalid` | — | `0x00` (rejected upstream) |

These values match `enum class Command` in `Legion2/Somfy_Remote_Lib/src/SomfyRemote.h` line 7-17. Without the fix, sending a `program` command would emit a frame with `button=0x80` (which means "the upper nibble of frame[1] is 0x80" — outside the Somfy command set), and the motor would not enter pairing mode.

## RollingCodeStorage = nullptr

`SomfyRemote::sendCommand(cmd)` uses the storage to fetch the next code. We avoid that path by always calling `sendCommandWithCode(cmd, code)` which doesn't touch the storage. Passing `nullptr` to the constructor is safe in this case (the field is stored but never dereferenced).

Rationale: we already persist the rolling code in NVS via `nvs_store::update_rolling_code()`, called by the orchestrator BEFORE the RF emission (persist-before-emit pattern, iter 004). Having a second source of truth in the lib's `NVSRollingCodeStorage` would risk divergence on partial writes.

## Failure handling

| Condition | Behaviour |
|---|---|
| `rf::init()` failed at boot (no CC1101) | `s_ready = false`, `send_somfy()` returns false, orchestrator logs `rf::send_somfy failed` and stops the chain (no MQTT state update, no rolling code commit — wait, the orchestrator persists first). |
| `button == 0` | `send_somfy()` returns false ; orchestrator already filtered this case. |
| Bit-banging interrupted | The lib wraps the sensitive parts in `noInterrupts()`. ESP32 WiFi runs on the radio core ; brief CPU IRQ disable doesn't drop association. |
