# Plan 013

## Steps

1. Edit `src/web_ui.cpp` JS handler : add the `confirm()` gate on `program7s` clicks before the row is disabled.
2. `pio run` both envs + `pio check` + `pio test -e native`.
3. HW flash on the C3.
4. Manual test in the browser (procedure below).
5. PR + CI green + merge.

## Test plan

### Native

No new native tests. The change is JavaScript inside a PROGMEM string ; the build verifies the string is syntactically correct (no missing quote / backtick). Existing 46 cases stay green.

### Manual (browser)

| Case | Action | Expected |
|---|---|---|
| **Erase prompt** | Click 🗑 Erase on remote A | A native confirm modal opens, explaining the 2-remote workflow and naming A as the source |
| **Cancel** | Click Cancel in the modal | No POST is sent, the row's buttons stay enabled, no row disable |
| **Confirm** | Click OK in the modal | Existing 7 s emission flow runs (row disabled, HTTP 204, TX_MS wait, then re-render) |
| **Other buttons untouched** | Click ▲ / ■ / ▼ / 🔗 Prog / ➕ Pair | No modal, immediate emission as before |
