# Plan 004

## Steps

1. Create `include/rf.h` + `src/rf.cpp` with a stubbed `init()` and `send_somfy()` (logs `[rf] STUB tx id=... code=... button=0x..`).
2. Create `include/orchestrator.h` with `handle_command()` and the inline `command_to_button()` helper.
3. Create `src/orchestrator.cpp` implementing the chain: NVS lookup → update rolling code → `rf::send_somfy` → publish state + rolling_code retained.
4. Add `test/test_orchestrator/test_main.cpp` covering `command_to_button` for the 4 valid commands + `Invalid`.
5. Update `src/main.cpp`: remove the stub `default_command_handler`; call `rf::init()`; pass `orchestrator::handle_command` to `mqtt::init()`.
6. Run `pio run` (zero warnings), `pio check` (zero defects), `pio test -e native` (all green).
7. Flash and verify HW (table below).

## Test plan

### Native (Unity)

| Test | Scenarios |
|---|---|
| `command_to_button_up` | `Command::Up` → `0x02` |
| `command_to_button_down` | `Command::Down` → `0x04` |
| `command_to_button_stop` | `Command::Stop` → `0x01` (Somfy "MY") |
| `command_to_button_program` | `Command::Program` → `0x80` |
| `command_to_button_invalid` | `Command::Invalid` → `0x00` |

### HW (manual, requires the board + mosquitto on the LAN)

Subscribe in one terminal: `mosquitto_sub -h 192.168.0.230 -t 'somfy2mqtt/#' -v`.

| Case | Action | Expected on serial | Expected on broker |
|---|---|---|---|
| **Add test remote** | Open `http://192.168.0.86/`, add remote `A1B2C3` with name "test" | `[web] remote +A1B2C3 name=test` | — |
| **Known remote, valid command** | `mosquitto_pub -t somfy2mqtt/A1B2C3/set -m up` | `[mqtt] cmd id=A1B2C3 cmd=up` → `[rf] STUB tx id=A1B2C3 code=1 button=0x02` → `[orch] executed up on A1B2C3 code=1` | `somfy2mqtt/A1B2C3/state = up` (retained), `somfy2mqtt/A1B2C3/rolling_code = 1` (retained) |
| **Counter increments** | Repeat the publish four more times | `code=2`, `code=3`, `code=4`, `code=5` in subsequent logs | `rolling_code` retained = 5 |
| **Persistence across reboot** | Reset the board, then re-publish | code starts from the stored value + 1, not from 0 | `rolling_code` retained continues incrementing |
| **Unknown remote** | `mosquitto_pub -t somfy2mqtt/FFFFFF/set -m up` | `[orch] unknown remote FFFFFF, drop` | No new retained, no state |
| **Each Somfy button** | Publish `up`, `down`, `stop`, `program` and observe button hex | `button=0x02`, `0x04`, `0x01`, `0x80` respectively | `state` updates accordingly |
