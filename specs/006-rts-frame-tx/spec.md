# 006 — first real Somfy RTS frame

## Goal
Replace the `rf::send_somfy` stub (from iter 004) with real emission via `Legion2/Somfy_Remote_Lib` + `ELECHOUSE_cc1101`. First Somfy RTS frame on 433.42 MHz.

## Validation
- SDR (RTL-SDR + Universal Radio Hacker) to decode the emitted frame
- **OR** PROG mode on a real Somfy motor: long-press the button at the back of the shutter → send PROG from the ESP → the motor performs a confirm up-and-down cycle

## Target modules
`src/rf.cpp` (replaces the stub emission)

## Status
Backlog — full spec / architecture / plan to be written at iteration start.
