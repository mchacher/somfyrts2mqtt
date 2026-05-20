# 003 — mqtt client

## Goal
Connect/reconnect to the MQTT broker (config from NVS), subscribe to `somfy2mqtt/+/set`, publish state as retained, declare a last-will. Incoming commands are dispatched to a handler that only logs for now.

## Target modules
`include/mqtt.h`, `src/mqtt.cpp`

## Status
Backlog — full spec / architecture / plan to be written at iteration start.
