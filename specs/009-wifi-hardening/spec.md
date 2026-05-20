# 009 — wifi hardening (Tasmota-style)

## Goal
Make boot WiFi connect fast and predictable on networks with multiple APs sharing the SSID (mesh). Avoid the multi-second "trial-and-error roaming" we observed on `GrangeNeuve_Freebox_2.4Ghz` (2 BSSIDs on the SSID → ~12 s to connect with default `WiFi.begin`).

Apply the Tasmota pattern: scan + pin the highest-RSSI BSSID, persist it in NVS, fast-path reconnect on subsequent boots. Also fold in the small hardening fixes from the WiFi review (persistent off, hostname, sleep off, decoded reasons).

## Scope

**In scope:**
- `wifi_manager`: full rewrite of `init()` with the scan+pin pattern. Same public API (`init()`, `loop()`, `is_connected()`).
- `nvs_store`: add `WifiHint` (bssid, channel, ssid) + `get_wifi_hint()` / `set_wifi_hint()` / `clear_wifi_hint()`.
- Fast path on boot: if NVS hint exists and matches `WIFI_SSID`, call `WiFi.begin(ssid, pass, channel, bssid)` directly (no scan).
- Slow path on first boot (or after `clear_wifi_hint()`): scan all 2.4 GHz APs, pick the strongest matching `WIFI_SSID`, save to NVS, connect to it.
- Hardening: `WiFi.persistent(false)`, `setHostname("somfyrts2mqtt-<3 bytes mac>")`, `setSleep(false)`, decoded disconnect reasons (humanly readable).
- Auto-invalidate the hint on `STA_DISCONNECTED` with reasons that suggest the AP changed (`BEACON_TIMEOUT`, `NO_AP_FOUND`, `ASSOC_FAIL`).
- Bump the busy-wait in `main.cpp` from 5 s to 15 s so the web UI gets the right IP in its log on slow networks.

**Out of scope:**
- Dual-SSID fallback (Tasmota AP1/AP2). One network is enough.
- WiFi credentials editing through the web UI (already in iter 007 scope ; can be added separately).
- WPS / SmartConfig / AP-fallback provisioning.
- Adaptive backoff with random jitter (Tasmota does this for multi-bridge deployments ; not needed here).

## Acceptance criteria
- [ ] Build clean (zero warnings) on `esp32-c3-mini` and `esp32-wroom`.
- [ ] `pio check` zero defects on both envs.
- [ ] `pio test -e native` all green (existing tests unchanged ; small additions for BSSID hex helpers if any).
- [ ] On a fresh-NVS C3, first boot scans, picks the best BSSID, and connects in **under 10 s** (scan 5-6 s + connect 2-4 s).
- [ ] On a subsequent boot (NVS hint already saved), connects in **under 5 s** (no scan, just direct begin).
- [ ] Serial shows decoded disconnect reasons (e.g. `disconnected reason=2 (AUTH_EXPIRE)`).
- [ ] Hostname appears as `somfyrts2mqtt-XXYYZZ` in the router DHCP table.
- [ ] If the saved BSSID is gone (router moved channels, AP unplugged), the firmware re-scans within ~15 s and finds the new best.
- [ ] CI green on the PR.

## Decisions
- **Scan synchronously in `init()`** (not async): keeps the code simple. The ~6 s of scan only run on first boot and on hint invalidation.
- **NVS hint scoped to the current `WIFI_SSID`**: if the user changes the SSID in `secrets.h`, the stored hint is ignored (mismatch) and a fresh scan runs.
- **Hint invalidation on specific disconnect reasons only**: `BEACON_TIMEOUT`, `NO_AP_FOUND`, `ASSOC_FAIL`. We don't drop the hint on `HANDSHAKE_TIMEOUT` or `AUTH_EXPIRE` (those are usually transient ; the core auto-reconnect retries on the same BSSID).
- **No dual-SSID** : keeps the scope tight ; the user can add it later (Tasmota toggles AP1/AP2 with `Settings->sta_active ^= 1`).
