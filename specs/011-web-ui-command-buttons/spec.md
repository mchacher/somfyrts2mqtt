# 011 — web UI command buttons (Up / Stop / Down / Prog)

## Goal
Add four per-remote command buttons in the web UI (`Up`, `Stop`, `Down`, `Prog`). Clicking a button triggers the same end-to-end chain as an external MQTT publish — but **without going through the broker**: the handler calls `orchestrator::handle_command(id, cmd)` directly. That keeps the bridge usable when MQTT is unreachable (pairing, on-site control, etc.) and avoids the round-trip latency.

## Scope

**In scope:**
- New REST endpoint: `POST /api/remotes/<id_hex>/command` with body `{"cmd": "up" | "down" | "stop" | "program"}`. Returns `204` on success, `400` on validation, `404` if the remote is unknown.
- Server-side handler validates inputs, decodes the command, calls `orchestrator::handle_command(id, cmd)` directly.
- Web UI: four icon buttons appended to each row in the Remotes table. Each button POSTs to the endpoint above and refreshes the row's rolling_code on success.
- Bug fix: the existing `POST /api/remotes` allows `id_hex` collisions — keep behaviour but ensure `command_buttons` use the canonical row id (the one returned by `GET /api/remotes`).

**Out of scope:**
- "Group" buttons that fan a command to multiple remotes — Sowel side's job.
- Adjustable repeat count per command (Legion2 default = 4, fine for now).
- Confirmation dialog before sending `Prog` — it's not destructive on Somfy, and the user only sends it during pairing.
- WebSocket push for state changes — the UI re-fetches `/api/remotes` after a command to update the rolling_code column.

## Acceptance criteria
- [ ] Build clean on `esp32-c3-mini` and `esp32-wroom`. `pio check` zero defects. `pio test -e native` all green (no native test changes needed).
- [ ] On the web UI, each remote row shows the four buttons. Clicking `Up` makes the corresponding shutter go up. Same for `Stop`, `Down`, `Prog`.
- [ ] After a click, the rolling_code in the row updates within ~500 ms (the JS re-fetches `/api/remotes`).
- [ ] When the broker is unreachable, the buttons still work (chain doesn't depend on MQTT).
- [ ] The retained MQTT messages `somfy2mqtt/<id>/state` and `somfy2mqtt/<id>/rolling_code` are still published (when the broker is up) — the orchestrator is the canonical path for both UI clicks and MQTT sets.
- [ ] CI green on the PR.

## Decisions
- **Direct orchestrator call**, not a MQTT publish loop-back. Two reasons:
  - Resilience: works without the broker (useful for first-time pairing, on-site debug).
  - Simplicity: no need to subscribe to ourselves or chase race conditions.
- **`Prog` button included** since the user's main pairing workflow is "PROG long-press on existing remote, then PROG from the bridge". Having it one click away on the phone beats running `mosquitto_pub` from a laptop.
- **No `MyUp` / `MyDown` / `SunFlag` / `Flag` buttons** — the lib supports them but Somfy RTS shutters only respond to the basic four. Keeping the UI focused.
- **Plain HTML icons (▲ ■ ▼ 🔗) embedded inline** — no font icon library, no external CSS file. Keeps the page <10 KB.
- **No confirmation modal for `Prog`** — clicking PROG with no motor in pairing mode emits a frame that's ignored by all paired receivers. Safe.
