# Architecture 011

## Touched modules

| File | Change |
|---|---|
| `src/web_ui.cpp` | New endpoint `POST /api/remotes/<id_hex>/command` + 4 buttons in the HTML/JS |
| `include/orchestrator.h` | Add `command_from_str()` inline helper (parses "up" / "stop" / "down" / "program") |
| `test/test_orchestrator/test_main.cpp` | 5 native tests for `command_from_str` |

No change to `mqtt`, `rf`, `nvs_store`, `wifi_manager`, `config.h`, `platformio.ini`.

## REST API addition

| Method | Path | Body | Response |
|---|---|---|---|
| `POST` | `/api/remotes/<id_hex>/command` | `{"cmd": "up" \| "down" \| "stop" \| "program"}` | `204 No Content` ; `400` if `cmd` invalid ; `404` if `id_hex` not found |

`id_hex` is the 6-char uppercase hex used everywhere else.

## Public API addition (orchestrator)

```cpp
namespace orchestrator {
  // Existing:
  void handle_command(uint32_t remote_id, mqtt::Command cmd);
  inline uint8_t command_to_button(mqtt::Command cmd);

  // New:
  inline mqtt::Command command_from_str(const char* s);
}
```

`command_from_str` is **case-insensitive**, recognises "up", "down", "stop", "program", returns `mqtt::Command::Invalid` otherwise (including `nullptr`).

## Flow

```
Click "Up" on remote A1B2C3 in the browser
  └─ JS POST /api/remotes/A1B2C3/command {"cmd":"up"}
       └─ web_ui::handle_command_post (new)
            ├─ parse id_hex -> remote_id (uint32_t)
            ├─ verify nvs_store::get_remote(remote_id) succeeds (404 if not)
            ├─ parse body.cmd -> mqtt::Command (400 if invalid)
            ├─ orchestrator::handle_command(remote_id, cmd)
            │     └─ ... same chain as MQTT-driven (NVS persist + rf + retained publish)
            └─ respond 204

JS post-success
  └─ re-fetch /api/remotes, update the rolling_code cell
  └─ re-fetch /api/status, update the bridge status panel
```

## HTML / JS additions

In the embedded HTML, the remotes `<tbody>` row template gains a 5th `<td>` (or extends the existing `<td>` with the delete button) containing four buttons:

```html
<td class="cmd-cell">
  <button data-cmd="up"      title="Up">▲</button>
  <button data-cmd="stop"    title="Stop">■</button>
  <button data-cmd="down"    title="Down">▼</button>
  <button data-cmd="program" title="Pair (PROG)">🔗</button>
</td>
```

JS handler (vanilla, no framework, ~10 lines):
```js
body.querySelectorAll('.cmd-cell button').forEach(b => {
  b.onclick = async () => {
    try {
      await fetchJSON(`/api/remotes/${id}/command`,
        {method:'POST', headers:{'Content-Type':'application/json'},
         body: JSON.stringify({cmd: b.dataset.cmd})});
      await loadRemotes();
      await loadStatus();
    } catch (e) { show('#remotes-msg','err', e.message); }
  };
});
```

A small `.cmd-cell button` CSS block makes the buttons fit on one line (40px wide, slightly larger touch target on mobile).

## Validation rules

| Field | Rule | Failure response |
|---|---|---|
| `id_hex` (URL) | exactly 6 hex chars | `400 Bad Request` |
| `id_hex` exists | must be in `r.index` | `404 Not Found` |
| `cmd` (body) | one of `"up" / "down" / "stop" / "program"` (case-insensitive) | `400 Bad Request` |

## Why direct orchestrator call

A MQTT loop-back (web UI publishes to `somfy2mqtt/<id>/set`, then receives it back via its own subscription) would:
- Add a round trip through the broker (latency, broker dependency)
- Break when the broker is unreachable (precisely the case we want the UI buttons to cover)
- Create a feedback loop risk if the dispatcher publishes a state that re-triggers

Calling `orchestrator::handle_command()` directly is the canonical entry point — same code path as MQTT, no duplication.
