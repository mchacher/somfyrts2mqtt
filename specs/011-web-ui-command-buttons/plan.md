# Plan 011

## Steps

1. Add `command_from_str()` inline helper to `include/orchestrator.h`. Case-insensitive, recognises `up` / `down` / `stop` / `program`, returns `mqtt::Command::Invalid` otherwise.
2. Add native tests for `command_from_str` in `test/test_orchestrator/test_main.cpp`.
3. In `src/web_ui.cpp`:
   - Add `handle_command_post(req, json)` handler.
   - Register the route `s_server.on("^/api/remotes/([0-9A-Fa-f]{6})/command$", HTTP_POST, ...)`. Async JSON body via `AsyncCallbackJsonWebHandler` if needed -- or read raw body since the handler is registered with the regex variant.
   - Update the embedded HTML: add a `cmd-cell` column to the remotes table, with 4 buttons.
   - Update the JS `loadRemotes()` to render the buttons and wire the click handler.
   - Add minimal CSS for the new column.
4. Run `pio run` + `pio check` on both envs. `pio test -e native` must add the 5 new cases and stay green.
5. Flash on the C3, open the web UI in a browser, click each button on the paired remote.

## Test plan

### Native (Unity)

| Test | Scenarios |
|---|---|
| `command_from_str_up` | "up", "UP", "Up" → `Command::Up` |
| `command_from_str_down` | "down" → `Command::Down` |
| `command_from_str_stop` | "stop" → `Command::Stop` |
| `command_from_str_program` | "program" → `Command::Program` |
| `command_from_str_invalid` | "", "xxx", `nullptr`, "ups" → `Command::Invalid` |

### HW (browser session)

Preconditions: the C3 + CC1101 + SMA antenna boot fine ; remote `A1B2C3` is paired with a real shutter (done in iter 006 test).

| Case | Action | Expected |
|---|---|---|
| **Buttons rendered** | Open `http://192.168.0.75/` | The Remotes table shows the paired remote with 4 buttons (▲ ■ ▼ 🔗) |
| **Up** | Click ▲ | Shutter goes up. Rolling_code in the row increments by 1. Broker sees `somfy2mqtt/A1B2C3/state = up`. |
| **Stop** | Click ■ | Shutter stops. Rolling_code +1. |
| **Down** | Click ▼ | Shutter goes down. Rolling_code +1. |
| **Pair new remote (Prog)** | Add a new remote in the UI (e.g. `C1C2C3`), put a Somfy shutter in pairing mode via the existing remote, click 🔗 on the C1C2C3 row | Shutter confirms by jogging. Pairing done via UI only. |
| **Unknown id (URL hack)** | Manually POST to `/api/remotes/FFFFFF/command` | `404` |
| **Bad cmd** | POST `{"cmd":"jump"}` | `400`, error JSON |
| **MQTT down + button clicks** | Stop mosquitto, then click ▲ | Shutter still moves (orchestrator path doesn't depend on the broker). Only the retained MQTT publish silently fails. |
