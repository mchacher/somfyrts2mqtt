# Plan 006

## Steps

1. Fix `orchestrator::command_to_button` in `include/orchestrator.h`: `Program` returns `0x08` (was `0x80`).
2. Update `test/test_orchestrator/test_main.cpp` so the `program` test expects `0x08`.
3. Rewrite `rf::init()` in `src/rf.cpp` to extend the CC1101 ping with OOK + TX mode configuration (`setModulation(2)`, `setPA(10)`, `setSyncMode(0)`, `setPktFormat(3)`, `SetTx()`, `pinMode(GDO0, OUTPUT)`).
4. Rewrite `rf::send_somfy()` to construct a transient `SomfyRemote(CC1101_GDO0, remote_id, nullptr)` and call `sendCommandWithCode(static_cast<::Command>(button), rolling_code, 4)`.
5. Run `pio run` + `pio check` on both envs ; `pio test -e native` must stay green (33 cases including the corrected `command_to_button_program`).
6. Flash on the C3 with CC1101 + SMA antenna ; verify boot log shows the CC1101 ok + the OOK config log.
7. HW pairing + control test (table below).

## Test plan

### Native (Unity)

Existing test `test_command_to_button_program` updated to assert `0x08`. The rest of the suite (33 cases total) is unchanged.

### HW (manual, with an existing Somfy remote that already controls at least one shutter)

Preconditions:
- ESP32-C3 + CC1101 module wired per CLAUDE.md.
- **SMA antenna fixed to the CC1101**.
- Web UI reachable on `http://<bridge-ip>/`.
- A working Somfy RTS shutter and its physical remote.

| Case | Action | Expected |
|---|---|---|
| **Boot config** | Flash, monitor serial | `[rf] cc1101 ok part=0x00 version=0x14 freq=433.42 MHz` then a second line confirming OOK setup (mod=2 pa=10 pkt_format=3) |
| **Add a virtual remote** | Open the web UI, add a remote with id `A1B2C3` and name `test` | Remote appears in the list ; NVS persists across reboot |
| **Pair the bridge** | (a) Long-press PROG on the back of the existing Somfy remote (~2 s) until the motor jogs once. (b) Within 2 minutes, publish `mosquitto_pub -t somfy2mqtt/A1B2C3/set -m program`. | The motor jogs **a second time** to confirm. Serial: `[rf] tx id=A1B2C3 code=1 button=0x08`. The virtual remote is now associated with the motor. |
| **Up** | `mosquitto_pub -t somfy2mqtt/A1B2C3/set -m up` | The shutter goes up. Serial: `[rf] tx ... button=0x02`. Broker shows `somfy2mqtt/A1B2C3/state = up` and `rolling_code = 2`. |
| **Stop** | `mosquitto_pub -t somfy2mqtt/A1B2C3/set -m stop` | The shutter stops. Serial button=0x01. |
| **Down** | `mosquitto_pub -t somfy2mqtt/A1B2C3/set -m down` | The shutter goes down. Serial button=0x04. |
| **Persistence across reboot** | Reset the board, then publish `up` again. | Shutter still moves ; rolling code keeps incrementing (5, 6, ...). Motor accepts because the code is monotonic. |
| **Unknown id** | `mosquitto_pub -t somfy2mqtt/FFFFFF/set -m up` | `[orch] unknown remote FFFFFF, drop` ; no RF emission, no motor reaction. |

If the pairing test fails (motor does not jog the 2nd time), the most likely causes are:
- Antenna not soldered / SMA not screwed in properly → no signal radiated.
- Bridge too far from the receiver → move closer for the test.
- The PROG window timed out (2 min) → restart the pairing on the existing remote.
- CC1101 PA misconfigured → check the boot log for the OOK setup line.
