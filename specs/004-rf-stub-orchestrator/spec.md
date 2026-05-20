# 004 — rf (stub) + orchestrator

## Goal
`rf` module with `rf::send_somfy()` **stubbed** (logs instead of emitting). Wire the full chain: MQTT received → NVS lookup of the remote → `rf::send_somfy` → increment `rolling_code` → re-persist → publish state retained.

At the end of this iteration the firmware is **functionally complete** on the business-logic side; only the real RF emission is missing (iter 006).

## Target modules
`include/rf.h`, `src/rf.cpp` (stub), `src/orchestrator.cpp`

## Status
Backlog — full spec / architecture / plan to be written at iteration start.
