# Plan 009

## Steps

1. Add `WifiHint` struct + `get_wifi_hint()` / `set_wifi_hint()` / `clear_wifi_hint()` to `nvs_store.h` and `.cpp`.
2. Rewrite `src/wifi_manager.cpp` with: hardening, hostname, decoded reasons, scan + pick best BSSID, NVS hint fast path, hint invalidation on relevant disconnect reasons.
3. Update `src/main.cpp`: bump the WiFi wait loop from 5 s to 15 s (cheap fix while we're here).
4. `pio run -e esp32-c3-mini` and `pio run -e esp32-wroom` — both clean.
5. `pio check -e esp32-c3-mini` and `pio check -e esp32-wroom` — zero defects.
6. `pio test -e native` — still green.
7. HW: flash on a C3, verify the boot sequence below twice (cold then warm).

## Test plan

### Native (Unity)

No new tests required — the additions are hardware-coupled (scan, `WiFi.begin`, NVS BSSID R/W). Existing 31 tests still pass.

### HW (manual)

| Case | Action | Expected serial |
|---|---|---|
| **Cold boot, no NVS hint** | `nvs_store::factory_reset()` once (or use a fresh board), then flash and monitor | `[wifi] no hint, scanning ...` → `[wifi] N APs visible, target ... best bssid=... ch=N rssi=...` → `[wifi] pinning bssid=... hostname=...` → `[wifi] connected ip=... in 2-4 s` |
| **Warm boot, hint saved** | Reset the board (already booted once with the hint saved) | `[wifi] hint bssid=... ch=N hostname=...` → `[wifi] connected ip=... in ~2 s` (no scan) |
| **Hint stale (AP moved channel)** | Reboot the AP forcing a new channel, then reset the board | `[wifi] hint ...` → `[wifi] disconnected reason=200 (BEACON_TIMEOUT)` → next reboot uses slow path again |
| **Decoded reasons** | Power off the AP momentarily | Log shows e.g. `[wifi] disconnected reason=200 (BEACON_TIMEOUT)` |
| **Hostname** | Check the router's DHCP client table | `somfyrts2mqtt-XXYYZZ` (with the last 3 MAC bytes) |
| **Web UI startup** | After cold boot, refresh `http://<ip>/` | `[web] listening on http://192.168.0.XX/` (real IP, not `0.0.0.0`) thanks to the 15 s wait |
