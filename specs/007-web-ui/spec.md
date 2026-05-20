# 007 — web UI (MVP)

## Goal
Local-network web UI on the bridge so the broker config and the virtual remotes can be edited from a browser, without recompiling. Replaces the `secrets.h` MQTT bootstrap and removes the need for a "seed test remote" hack in the upcoming iter 004 (orchestrator).

## Scope

**In scope:**
- `web_ui` namespace, embedded `AsyncWebServer` on port 80, STA mode (uses the IP from `wifi_manager`).
- Single-page UI served from `PROGMEM` (no LittleFS) — one binary, one upload.
- Three sections on the page:
  - **Status**: firmware version, IP, MAC, MQTT connection state, # remotes, uptime.
  - **MQTT config**: edit `host`, `port`, `user`, `pass`. Submit writes NVS and triggers a forced MQTT reconnect.
  - **Remotes**: list with id / name / rolling_code; "add" form (id hex + name); per-row delete.
- Factory reset button (with confirm) that calls `nvs_store::factory_reset()` then reboots.
- JSON REST API:
  - `GET /api/status`
  - `GET /api/mqtt`, `POST /api/mqtt`
  - `GET /api/remotes`, `POST /api/remotes`, `DELETE /api/remotes/<id_hex>`
  - `POST /api/factory_reset`
- Helper added to the `mqtt` module: `mqtt::disconnect()` so the next `mqtt::loop()` tick reconnects with the new NVS config.

**Out of scope:**
- AP fallback mode (first-boot or WiFi-down). Future iter.
- WiFi credentials editing (still through `secrets.h` for now).
- "Send up/down/stop" buttons — those need iter 004 (orchestrator). The page will get a fourth section once the orchestrator lands.
- Authentication / HTTPS (LAN-only trusted network).
- Pairing flow (sending a PROG frame to a motor) — needs iter 006.

## Acceptance criteria
- [ ] Build clean (zero warnings), `pio check` zero defects, native tests still green.
- [ ] On boot, serial logs `[web] listening on http://<ip>/`.
- [ ] `http://<board-ip>/` renders the status / MQTT / remotes page in a desktop or mobile browser.
- [ ] Editing the MQTT config and submitting persists it (verifiable by reading `/api/mqtt` again or rebooting) and triggers a MQTT reconnect within ~5 seconds (serial shows `[mqtt] disconnected, will retry` then `[mqtt] connected`).
- [ ] Adding a remote with id `B1B2B3` and name "salon" makes it appear in `/api/remotes` and survive a reboot.
- [ ] Deleting it removes it from `/api/remotes` and from `r.index` in NVS.
- [ ] Factory reset wipes the NVS namespace and reboots; on next boot the page is empty (zero remotes, MQTT defaults).
- [ ] CI green on the PR.

## Decisions
- **`ESPAsyncWebServer` + `AsyncTCP` (esp32async fork)** — already declared in `CLAUDE.md` as the planned web stack. Async fits the non-blocking `loop()` model.
- **`ArduinoJson@^7`** for request/response serialisation.
- **HTML/CSS/JS embedded in C++ as a `PROGMEM` constant** — one upload, no LittleFS to manage. Trade-off: less iterable than a `data/` dir, but the page is small (<10 KB) and rarely changes.
- **`POST /api/mqtt` does the reconnect for the caller** — the web UI handles `mqtt::disconnect()` internally. Same for factory_reset (handles the reboot itself).
- **`POST /api/factory_reset` reboots** via `ESP.restart()` after wiping. Otherwise the in-memory state would diverge from NVS.
- **Vanilla JS, no framework**. The page is small enough that a SPA framework would be massive overkill.
- **No auth / HTTPS** — the bridge sits on the LAN behind a trusted router. If we later expose the bridge to a hostile network, the AP-fallback iter is the right place to add auth.
- **HTML is inline in `src/web_ui.cpp`** (one big `R"raw(...)raw"` string). Splitting into a header would only add files without value.
