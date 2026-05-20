# Architecture 010

## Touched modules

| File | Change |
|---|---|
| `include/mqtt.h` | No public API change |
| `src/mqtt.cpp` | Add socket / keepalive setup, exponential backoff, decoded state log |

No new dependency, no NVS layout change, no config.h change.

## Backoff state machine

```
state.retry_ms      = 5000           // current delay before next attempt
state.last_attempt  = 0              // millis() at last connect attempt
state.was_connected = false          // for transition logging
```

On every `loop()`:

```
if !wifi.connected:                            return
if !mqtt.connected:
  if was_connected:
    log "disconnected, will retry"
    was_connected = false
  if now - last_attempt >= retry_ms:
    last_attempt = now
    if try_connect():
      was_connected = true
      retry_ms = 5000                           // reset on success
    else:
      retry_ms = min(retry_ms * 2, 60000)       // exponential, capped
      log "connect failed rc=%d (%s) -- next attempt in %lus"
else:
  client.loop()
```

## PubSubClient state code mapping

Used in `state_str(int rc)`:

| rc | Symbol | Meaning |
|---|---|---|
| -4 | `CONNECTION_TIMEOUT` | Server didn't reply within keep-alive |
| -3 | `CONNECTION_LOST` | Network connection broke |
| -2 | `CONNECT_FAILED` | TCP-level connect failed (timeout / refused) |
| -1 | `DISCONNECTED` | Client called `disconnect()` |
| 0 | `CONNECTED` | OK |
| 1 | `CONNECT_BAD_PROTOCOL` | Server doesn't support MQTT 3.1.1 |
| 2 | `CONNECT_BAD_CLIENT_ID` | Server rejected the client id |
| 3 | `CONNECT_UNAVAILABLE` | Server unavailable |
| 4 | `CONNECT_BAD_CREDENTIALS` | Bad user/pass |
| 5 | `CONNECT_UNAUTHORIZED` | Client not authorised |

## Socket / keep-alive tuning

In `init()`, before any `connect()` call:

```cpp
s_client.setBufferSize(256);          // already present, kept
s_client.setSocketTimeout(15);        // NEW: 15 s (default 3 s)
s_client.setKeepAlive(60);            // NEW: 60 s (default 15 s)
```

The TCP socket timeout governs the `connect()` blocking window — too tight and a slow router gets dropped before TCP handshake completes. Keep-alive sets the MQTT-level PINGREQ cadence; 60 s reduces traffic and survives brief broker pauses.
