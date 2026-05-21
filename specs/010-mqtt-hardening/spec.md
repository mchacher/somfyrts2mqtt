# 010 — MQTT hardening (Tasmota-style)

## Goal
Make the MQTT client robust against transient broker / network failures: extend the TCP socket timeout, extend the MQTT keep-alive, and use exponential backoff between reconnect attempts. Decode PubSubClient state codes in the logs so we can tell at a glance whether a failure is a TCP timeout, a credential issue, or a protocol mismatch.

Triggered by the recent observation: when the broker was unreachable (subnet isolation between `GrangeNeuve_Garage_2.4GHz` and `sowelox`), the C3 spammed reconnect attempts every 5 s with the 3 s default TCP timeout and logged `rc=-2` with no human-readable explanation.

## Scope

**In scope:**
- `mqtt::init()`: set `client.setSocketTimeout(15)` and `client.setKeepAlive(60)` (defaults are 3 s / 15 s — too tight for flaky residential networks).
- `mqtt::loop()`: exponential backoff between connect attempts. Start at 5 s, double on each failure, cap at 60 s. Reset to 5 s on successful connect.
- Decoded PubSubClient state code in the warning log (e.g. `connect failed rc=-2 (CONNECTION_TIMEOUT)`).
- Helper `state_str(int rc)` covering the standard PubSubClient codes (-4..5).

**Out of scope:**
- DNS resolution caching (we use a raw IP, no DNS path).
- TLS / WSS (deferred until we expose the broker over WAN).
- A hard watchdog (Tasmota relies on the platform watchdog ; we'll do the same).
- Per-message QoS tuning (we stay on QoS 0 for now ; bridge state stays retained).

## Acceptance criteria
- [ ] Build clean (zero warnings) on both `esp32-c3-mini` and `esp32-wroom`.
- [ ] `pio check` zero defects on both.
- [ ] `pio test -e native` all green (existing 31 cases unchanged).
- [ ] When the broker is unreachable, retries are spaced by 5 s, 10 s, 20 s, 40 s, 60 s, 60 s... (capped). Verified visually in serial logs.
- [ ] When the broker comes back, the next attempt connects normally and the backoff resets to 5 s.
- [ ] Failed-connect log shows the decoded state: e.g. `connect failed rc=-2 (CONNECTION_TIMEOUT)`, `rc=4 (BAD_CREDENTIALS)`, etc.
- [ ] When the broker is reachable, the connect time is **unchanged** (~50 ms LAN) — the longer socket timeout only kicks in on slow/dead networks.
- [ ] CI green on the PR.

## Decisions
- **Exponential backoff with a 60 s cap** (Tasmota uses 120 s with a multiplier counter ; 60 s is enough for our LAN-only deployment and keeps recovery snappy when the broker comes back).
- **Reset on success** rather than "always full reset" — Tasmota actually keeps the multiplier ; we reset because we want quick recovery from a network blip.
- **`setSocketTimeout(15)`** instead of Tasmota's user-configurable value — keeps things simple, no NVS entry needed.
- **`setKeepAlive(60)`** instead of the default 15 s — tolerates a brief broker hiccup without dropping us, and reduces traffic load (one PINGREQ per minute instead of one every 15 s).
- **No DNS caching** — `MQTT_BROKER_HOST` is stored in NVS as an IP literal (today) ; if we ever take an FQDN, we'll revisit.
- **`state_str(rc)`** mirrors the names from `PubSubClient::state()` so the log line maps 1:1 to the lib documentation.
