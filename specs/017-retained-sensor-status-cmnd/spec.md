# 017 — Retained SENSOR + bridge-wide `Status` cmnd

## Goal

A subscriber that connects to the broker **after** the bridge boot must still receive the current state of every paired remote. Today the bridge publishes `tele/<root>/SENSOR` on each motion / state-change but with `retained=false`, so a late subscriber sees nothing until something moves — which is exactly the case for the Sowel `sowel-plugin-somfy-rts` integration on a quiet morning.

This iter ships two complementary fixes :

- **Retained SENSOR** : every `publish_sensor_aggregated()` call uses `retained=true`. The broker keeps the latest snapshot ; any new subscriber to `tele/<root>/SENSOR` gets it immediately on subscribe.
- **`cmnd/<root>/Status`** : a bridge-wide command that forces a fresh SENSOR publish. Lets a client (Sowel plugin, mosquitto_pub, custom dashboard) explicitly trigger a resync — useful when the retained message went stale (broker restart without storage, broker storage cleared, manual resync button).

## Background

Today the only retained topic is `tele/<root>/LWT`. SENSOR is published live (1 Hz during motion + on each state change + once on MQTT connect) but never retained, so a fresh subscriber sees only LWT and stays blind until motion happens. Fix : retain the SENSOR snapshot, and let clients ask for a refresh on demand.

The bridge already implements a 4-segment cmnd subscription `cmnd/<root>/+/+` for per-remote commands. For a bridge-wide command we add a parallel 3-segment subscription `cmnd/<root>/Status`. Tasmota uses the same naming convention (`cmnd/<device>/Status`).

## Scope

**In scope**

- Pass `retained=true` to `s_client.publish()` inside `publish_sensor_aggregated()` (line 292 of `src/mqtt.cpp`). All 4 call sites (connect, on cmnd, on periodic, on calibration) inherit the fix automatically — no need to thread the flag through.
- Add a second subscription `cmnd/<root>/Status` after the existing `cmnd/<root>/+/+`.
- In `on_message`, detect the bridge-wide Status topic before the per-remote `parse_cmnd_topic` path. On match, call `publish_sensor_aggregated()`. Payload is ignored (Tasmota convention accepts a level digit ; we just always publish the aggregated view since that's all we have).
- Update `docs/mqtt-api.md` : document the retained nature of SENSOR + the new `cmnd/<root>/Status` verb.
- Bump tag to `v0.2.0`.

**Out of scope**

- Per-remote `cmnd/<root>/<name>/Status` (would just duplicate `stat/<root>/<name>` which already exists for ack). No need.
- Status sub-commands (Tasmota's `Status 0..13`). The bridge has one thing to report, no need to multiplex.
- Persisting retain across broker restarts (that's the broker's job ; mosquitto with `persistence true` keeps it).
- Republishing SENSOR on each Wi-Fi reconnect when MQTT was already up. Already covered by `try_connect()`.

## Acceptance

- [ ] `mosquitto_sub -h <broker> -t 'tele/<root>/#'` immediately receives the latest SENSOR after subscribing, even hours after the last motion.
- [ ] `mosquitto_pub -h <broker> -t 'cmnd/<root>/Status' -m ""` triggers a fresh SENSOR publish.
- [ ] The Sowel plugin `sowel-plugin-somfy-rts` discovers all paired remotes at `start()` without anyone moving anything.
- [ ] No regression on per-remote `cmnd/<root>/<name>/<verb>` dispatch.
- [ ] Native unit test for the topic discrimination logic (bridge-wide vs per-remote).
- [ ] Tag `v0.2.0` triggers the release workflow → both `.bin` artifacts attached to the GH Release.
