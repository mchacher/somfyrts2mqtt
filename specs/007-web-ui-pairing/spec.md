# 007 — web UI pairing

## Goal
AsyncWebServer in AP fallback mode (first boot or when WiFi is down). Forms for: WiFi config, MQTT broker config, create/delete a virtual remote (`remote_id` + initial counter + name), send a manual PROG frame (motor learning).

## Target modules
`include/web_ui.h`, `src/web_ui.cpp`, HTML/CSS assets under `data/` (LittleFS)

## Status
Backlog — full spec / architecture / plan to be written at iteration start.
