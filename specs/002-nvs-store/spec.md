# 002 — nvs_store

## Goal
Persist MQTT broker config and up to 16 Somfy virtual remotes in ESP32 NVS via Arduino's `Preferences` library. Expose a typed API the upcoming iters (003 MQTT, 004 orchestrator) will consume.

## Scope

**In scope:**
- Namespace `nvs_store` with `init()` and a boot-time diagnostic log.
- MQTT config: host (string), port (uint16), user (string), pass (string).
- Virtual remotes (up to 16): id (24-bit), rolling_code (uint16), name (1..32 chars).
- API: get/set MQTT config; add/get/list/delete/update remote; factory reset.
- Schema versioning sentinel (`schema` key, value `1` for now) for forward compatibility.
- Validation: id ∈ ]0, 0xFFFFFF], name length 1..32, max 16 remotes.
- Native unit tests for the pure-logic helpers (hex format/parse, validation, index manipulation).

**Out of scope:**
- NVS encryption (deferred; could switch to `nvs_flash_secure` later).
- Schema migration code (will be written when bumping the version).
- Remote backup over MQTT (iter 004).
- Web UI or serial CLI to edit at runtime (iter 007).
- An `IStore` abstraction for testing CRUD wiring; we trust `Preferences` itself and only test the logic we own (validation + index manipulation).

## Acceptance criteria
- [ ] Compiles cleanly into firmware (`pio run -e esp32-c3-mini`, zero warnings).
- [ ] `pio check` reports zero defects.
- [ ] `pio test -e native` passes; covers hex format/parse, id/name validation, index add/remove/contains.
- [ ] On a fresh-NVS boot, serial prints `[nvs] ready schema=1 remotes=0`.
- [ ] CI green on the PR.

## Decisions
- **Single Preferences namespace `somfy`** with key prefixes (`mqtt.*`, `r.<id_hex>.*`, `r.index`). Rationale: keeps lookups simple, single `begin()` call, atomic close.
- **`r.index` (CSV of hex ids) is the authoritative remote list.** Arduino's `Preferences` API does not expose key enumeration cleanly; reaching into the raw `nvs_handle_t` would be brittle and bypass the wrapper.
- **`std::string` everywhere (not Arduino `String`)** for portability — the same API compiles in the native test env. Embedded uses `.c_str()` to interop with `Preferences`.
- **Schema version stored as `uint8_t`**, currently `1`. The `init()` path detects mismatch and refuses to operate (logs error, leaves `s_ready = false`). Migration logic comes when we bump the version.
- **No `IStore` abstraction.** CRUD wiring is shallow delegation to `Preferences`. We test the pure helpers in native; we trust `Preferences` for storage correctness.
