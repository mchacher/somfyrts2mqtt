# 005 — cc1101 init + ping

## Goal
Initialise SPI to the CC1101, read the `PARTNUM` register (expected `0x00`) and `VERSION` register (expected `0x14`), calibrate to 433.42 MHz. No emission yet — just validate that the module responds and the wiring is correct.

## Target modules
`src/rf.cpp` (replaces the stub init from iter 004)

## Prerequisites
CC1101 hardware on hand and wired.

## Status
Backlog — full spec / architecture / plan to be written at iteration start.
