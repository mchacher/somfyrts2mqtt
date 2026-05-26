# Architecture 020 — Drop ESP32 WROOM board

## Touched modules

| File                              | Change                                                                            |
| --------------------------------- | --------------------------------------------------------------------------------- |
| `platformio.ini`                  | Delete `[env:esp32-wroom]` block.                                                 |
| `.github/workflows/ci.yml`        | Remove `esp32-wroom` from the build matrix.                                       |
| `.github/workflows/release.yml`   | Drop the WROOM build step + the `firmware.bin` copy line for `esp32-wroom`.       |
| `include/config.h`                | Delete the WROOM CC1101 pinout block; keep C3 + the cppcheck-safe `#error` guard. |
| `src/wifi_manager.cpp`            | Drop the stale "Harmless on WROOM" comment.                                       |
| `README.md`                       | Remove WROOM mentions in supported boards / install snippets.                     |
| `CLAUDE.md`                       | Remove WROOM in HW context and pinout reminders.                                  |
| `docs/hardware.md`                | Remove the WROOM pinout / wiring tables.                                          |
| `docs/releasing.md`               | Remove the WROOM bin from the artifact list.                                      |
| `docs/troubleshooting.md`         | Remove the WROOM-specific entry.                                                  |
| `docs/setup.md`                   | Remove the "pick your board" sentence and any WROOM-specific install step.       |

## Decisions

**Selected: hard removal across active code paths and live docs; historical
specs stay untouched.**

Specs 006/008/009/010/011/014/015/016/018 mention WROOM in passing
because they were the operating state at the time. Rewriting them would
be revisionist and offers no benefit — readers should still see what
the project supported in earlier iterations. New specs (this one and
onwards) reflect the C3-only world.

**Rejected: keeping the WROOM env behind a `keep_wroom = no` flag.**
Adds a second code path to maintain forever with zero users. If WROOM
support is ever needed again, it can be reintroduced in a future iter
based on the spec 008 multi-board history.

## Library + build impact

- `platformio.ini` loses one env. Default env stays `esp32-c3-mini`.
- CI build matrix goes from `[esp32-c3-mini, esp32-wroom]` to a single
  entry — roughly halves the CI build wall-time of the workflow.
- Release artifact set goes from two `.bin` files to one. The release
  notes already use the `vX.Y.Z` tag, no per-artifact name change.

## Flow

```
git checkout -b chore/drop-wroom-board
edit files (above)
pio run -e esp32-c3-mini   # must stay clean
pio check                  # must stay clean
pio test -e native         # 74 / 74
git commit -am ...
gh pr create
```

No runtime behaviour change — the C3 firmware is byte-for-byte
equivalent before/after this change (only the build matrix and the
unused WROOM pinout disappear).
