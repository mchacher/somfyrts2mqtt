# Plan 007

## Steps

1. Add `esp32async/AsyncTCP`, `esp32async/ESPAsyncWebServer`, `bblanchon/ArduinoJson@^7` to `platformio.ini`.
2. Add `mqtt::disconnect()` to the mqtt module (h + cpp).
3. Create `include/web_ui.h` with the single `init()` function (Doxygen block).
4. Create `src/web_ui.cpp` with:
   - The embedded HTML/CSS/JS in a `PROGMEM` raw string.
   - The `AsyncWebServer` instance + route handlers for each REST endpoint.
   - JSON parsing helpers via `ArduinoJson`.
5. Wire `web_ui::init()` in `src/main.cpp` after `wifi::init()`.
6. Build (`pio run`), check (`pio check`), test (`pio test -e native`) — all green.
7. Flash, browse to `http://192.168.0.86/`, run through the HW test below.

## Test plan

### Native (Unity)

No new native tests — the web_ui code is mostly HTTP wiring + delegation to nvs_store / mqtt, both of which already have their own native coverage. Pure helpers, if any sneak in, will be covered.

### HW (browser session)

Start a broker watcher in one terminal: `mosquitto_sub -h 192.168.0.230 -t 'somfy2mqtt/#' -v`.

| Case | Action | Expected |
|---|---|---|
| **Server starts** | Flash, monitor serial | `[web] listening on http://192.168.0.86/` |
| **Page renders** | Open `http://192.168.0.86/` in a browser | Status / MQTT / Remotes page visible, with current values |
| **Status JSON** | Hit `http://192.168.0.86/api/status` | JSON with version, ip, mac, uptime, mqtt_connected=true, remotes_count=0 |
| **MQTT config submit (same host)** | In MQTT form, click Save without changing values | Serial: `[mqtt] disconnected, will retry` then `[mqtt] connected, subscribed ...`. Broker watch shows `bridge/state` flapping `online -> online`. |
| **MQTT config submit (bad host)** | Set host to `192.168.0.250` (does not exist), click Save | Serial: `[mqtt] disconnected` then repeated `[mqtt] connect failed rc=...`. UI status panel shows MQTT disconnected. Revert via UI to restore. |
| **Add remote** | Add id `B1B2B3`, name "salon" | Row appears. `/api/remotes` returns 1 entry. NVS persists across reboot. |
| **Delete remote** | Click Delete on the salon row | Row gone, `/api/remotes` empty. |
| **Capacity guard** | Add 16 remotes, then try a 17th | 17th submission → `409 Conflict` toast / error message; only 16 stored. |
| **Validation** | Try id `XYZ` or name with 40 chars | Form blocks submit (client-side) or server returns `400`. |
| **Factory reset** | Click Factory reset, confirm | Page shows reboot message, board restarts, NVS empty after reboot. |
