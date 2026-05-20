# 003 — mqtt client

## Goal
Connect (and reconnect) to the MQTT broker using the config persisted by iter 002. Subscribe to `somfy2mqtt/+/set` and dispatch incoming commands to a registered handler. Manage the bridge presence with a retained "state" topic + LWT.

## Scope

**In scope:**
- Namespace `mqtt` with `init()`, `loop()`, `is_connected()`, `set_command_handler()`.
- Connect via `knolleary/PubSubClient` using MQTT config from `nvs_store::get_mqtt()`.
- Reconnect loop (non-blocking, gated on `wifi::is_connected()`).
- Bridge presence: publish `somfy2mqtt/bridge/state` = "online" on connect (retained); LWT sets it to "offline" if the bridge disconnects.
- Subscribe to `somfy2mqtt/+/set`. Parse `<remote_id_hex>` and the payload command (`up`/`down`/`stop`/`program`). Dispatch to the handler.
- Default handler (until iter 004) logs the parsed command.
- Publish API ready for iter 004: `publish_state(remote_id, last_cmd)` and `publish_rolling_code(remote_id, code)` exposed but not auto-called.
- Native unit tests for the pure helpers (topic parse, command parse, topic build).
- Bootstrap pattern: if NVS has no MQTT host, copy `MQTT_BROKER_*` from `secrets.h` into NVS on first boot (one-shot seed).

**Out of scope:**
- Auto-publishing per-remote state (iter 004 — orchestrator owns when it happens).
- TLS / WSS / client cert auth.
- Multiple brokers, broker discovery.
- Web UI to edit MQTT config at runtime (iter 007).

## Acceptance criteria
- [ ] Compiles cleanly into firmware (zero warnings).
- [ ] `pio check` reports zero defects.
- [ ] `pio test -e native` passes; covers topic parse (valid + 4 invalid shapes), command parse (4 valid + invalid + empty + null), state topic build.
- [ ] On a fresh-NVS boot, the bootstrap path writes the broker config from `secrets.h` and the bridge connects. Serial shows: `[mqtt] connecting host=<x> port=<y>` → `[mqtt] connected` → `[mqtt] subscribed somfy2mqtt/+/set`.
- [ ] Publishing on `somfy2mqtt/A1B2C3/set` with payload "up" makes the firmware log `[mqtt] cmd id=A1B2C3 cmd=up`.
- [ ] LWT works: power-cycling the board makes `somfy2mqtt/bridge/state` flip to "offline" (retained), confirmed via MQTT Explorer or `mosquitto_sub`.
- [ ] CI green on the PR.

## Decisions
- **Library: `knolleary/PubSubClient`** — sync API, well-known, small footprint. Acceptable for our single-broker use case (no concurrent connections).
- **Bootstrap from `secrets.h`** for MQTT broker config until iter 007 introduces the web UI. The firmware seeds NVS once on first boot if `mqtt.host` is empty; subsequent runs read from NVS unchanged.
- **Bridge topic structure mirrors Zigbee2MQTT**: `somfy2mqtt/bridge/state` (retained, "online"/"offline" via LWT). Future: `somfy2mqtt/bridge/info` for JSON metadata (deferred).
- **Command handler via function pointer**, not virtual interface — embedded-friendly, no allocation, easy to swap in iter 004 (orchestrator replaces the default log-only handler).
- **Topic parsing as pure helpers** in the header so native tests cover them without linking `PubSubClient`.
- **Reconnect interval 5 seconds**, non-blocking via `millis()` watermark. No exponential backoff for now (broker is on the LAN; immediate retry is fine).
