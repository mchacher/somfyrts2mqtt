# Plan 010

## Steps

1. Add `state_str(int)` helper to `src/mqtt.cpp` (anon namespace).
2. In `init()`: `s_client.setSocketTimeout(15); s_client.setKeepAlive(60);` after the existing `setBufferSize(256)`.
3. Replace the fixed `RECONNECT_INTERVAL_MS = 5000` with a stateful `s_retry_ms` initialised to 5000 and doubled (capped at 60000) after each failed `try_connect()`. Reset to 5000 on success.
4. Update the failed-connect log to include the decoded state and the next-retry delay.
5. Run `pio run`, `pio check`, `pio test -e native` on both envs.

## Test plan

### Native (Unity)

`state_str` is testable natively — adding a small `test/test_mqtt_state/` Unity test that asserts the canonical names for the 10 codes.

### HW (manual, when the user is back)

| Case | Action | Expected |
|---|---|---|
| **Broker reachable** | Normal boot | Connect time unchanged (~tens of ms LAN), no backoff visible |
| **Broker down** | `sudo systemctl stop mosquitto` on sowelox while firmware is running | Log shows `connect failed rc=-2 (CONNECT_FAILED) -- next attempt in 5s` then `10s`, `20s`, `40s`, `60s`, `60s`... |
| **Broker comes back** | Restart mosquitto after a few cycles | Next attempt succeeds, log shows `connected, subscribed somfy2mqtt/+/set`, backoff resets |
| **Wrong credentials** | Temporarily set `MQTT_BROKER_USER` to a bad value in secrets.h | Log shows `connect failed rc=4 (CONNECT_BAD_CREDENTIALS)` — clear root cause |
| **Subnet isolation** (today's case) | C3 on a network that can't route to the broker | Log shows `rc=-2 (CONNECT_FAILED)` with growing backoff — same as a down broker |
