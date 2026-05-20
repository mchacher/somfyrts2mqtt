# Plan 002

## Steps

1. Create `include/nvs_store.h` with the full public API and structs (Doxygen blocks).
2. Implement the pure helpers in `src/nvs_store.cpp` (no `Preferences` calls): `is_valid_id`, `is_valid_name`, `format_id_hex`, `parse_id_hex`, `index_contains`, `index_add`, `index_remove`.
3. Add native unit tests under `test/test_nvs/test_nvs.cpp` for those helpers. Verify with `pio test -e native`.
4. Implement the `Preferences`-backed methods: `init`, `set_mqtt`, `get_mqtt`, `add_remote`, `update_rolling_code`, `delete_remote`, `get_remote`, `list_remotes`, `remotes_count`, `factory_reset`.
5. Wire `nvs_store::init()` in `src/main.cpp` between `logger` and `wifi`. Add the boot diagnostic log.
6. Run `pio run -e esp32-c3-mini` (zero warnings), `pio check` (zero defects), `pio test -e native` (all green).
7. Flash the firmware and verify the single HW case below.

## Test plan

### Native (Unity)

| Test | Scenarios |
|---|---|
| `format_parse_roundtrip` | id = 1, 0xABCDEF, 0xFFFFFF — format then parse, ensure equal |
| `parse_invalid` | "G12345" → fails ; "12345" (5 chars) → fails ; "1234567" (7 chars) → fails ; "" → fails |
| `is_valid_id` | 0 invalid ; 1 valid ; 0xFFFFFF valid ; 0x1000000 invalid |
| `is_valid_name` | "" invalid ; "a" valid ; 32-char valid ; 33-char invalid |
| `index_add_idempotent` | add "A1B2C3" twice on empty → idx == `"A1B2C3"` (no duplicate) |
| `index_add_two` | add "A1B2C3" then "D4E5F6" → idx == `"A1B2C3,D4E5F6"` |
| `index_remove_present` | idx = `"A1B2C3,D4E5F6"` ; remove "A1B2C3" → `"D4E5F6"` ; returns true |
| `index_remove_absent` | idx unchanged ; returns false |
| `index_contains` | true for present, false for absent, false for empty CSV |

### HW (manual, single case)

| Case | Action | Expected serial |
|---|---|---|
| **Cold boot** | Flash, monitor at 115200 | `[boot] hello somfyrts2mqtt` → `[nvs] ready schema=1 remotes=<n>` → `[wifi] connecting ssid=...` → `[wifi] connected ip=...` |

`<n>` ≥ 0 is fine — NVS may already contain prior data (NVS survives reflash, only erased by `esptool.py erase_flash` or `nvs_store::factory_reset()`). The point is the line appears in the right place with `schema=1`.
