# Plan 001

## Steps

1. Create `include/secrets.h.example` with two `#define` placeholders, add `include/secrets.h` to `.gitignore`
2. Create `include/secrets.h` locally (real credentials, not committed)
3. Implement `include/logger.h` + `src/logger.cpp` (~30 lines total)
4. Implement `include/wifi_manager.h` + `src/wifi_manager.cpp` (~50 lines)
5. Refactor `src/main.cpp` to orchestrate logger + WiFi
6. Build (zero warnings, zero errors) + flash + monitor serial

## Test plan (HW)

| Case | Action | Expected on serial |
|---|---|---|
| **Nominal** | Flash with a correct `secrets.h`, observe the boot | `[boot] hello somfyrts2mqtt` → `[wifi] connecting ssid=<x>` → `[wifi] connected ip=<x.x.x.x>` in under 10 s |
| **Reconnect** | While the firmware is running, power off the router for 30 s then power it back on | `[wifi] disconnected reason=<n>` then `[wifi] connected ip=<x.x.x.x>` automatically |
| **Missing secrets** | Temporarily rename `secrets.h` and run the build again | Clear compile error (`fatal error: secrets.h: No such file or directory`), not a runtime crash |
| **Wrong SSID** | Put a non-existent SSID in `secrets.h`, flash | `[wifi] connecting ssid=<bogus>` then repeated `[wifi] disconnected reason=...` every ~5 s |
