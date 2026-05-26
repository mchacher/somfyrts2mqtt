# CLAUDE.md — somfyrts2mqtt

Somfy RTS <-> MQTT bridge on ESP32-C3 + CC1101. Designed to integrate with [Sowel](https://github.com/mchacher/sowel) (plugin `sowel-plugin-somfy-rts`) following the Zigbee2MQTT pattern: the firmware is a "dumb" bridge that speaks MQTT, all business logic (groups, scenes, recipes) stays on the Sowel side.

## Language

All written content in this repo is **English**: code, comments, Doxygen blocks, docs, specs, commit messages, PR titles and bodies, GitHub Actions workflows. Conversation with the maintainer can be in French.

## Hardware

Two boards are supported. Each PlatformIO env selects its target chip; `include/config.h` picks the matching CC1101 pinout at compile time via `CONFIG_IDF_TARGET_*`.

- **RF (both boards)**: CC1101 module (433.42 MHz for Somfy RTS), 26 MHz crystal.
- **Antenna (both boards)**: 17.3 cm wire (1/4 wave) soldered to the CC1101 ANT pad.

### ESP32-C3 Super Mini — `pio run -e esp32-c3-mini`

- WiFi, BLE, native USB-CDC, RISC-V single-core, 4 MB flash.
- Strapping pins to avoid: **GPIO 2, 8, 9**.

| CC1101 module | ESP32-C3 Super Mini |
|---|---|
| VCC (3.3V) | 3V3 — **never 5V (max 3.6V)** |
| GND | GND |
| SCK | GPIO4 |
| MISO (silkscreen reads "MOSI/GD01") | GPIO5 — module label is wrong, this is MISO |
| MOSI | GPIO6 |
| CSN | GPIO7 |
| GDO0 | GPIO10 |
| GDO2 | GPIO3 (optional, RX sniff) |

## Build / flash / monitor

```bash
~/.platformio/penv/bin/pio run                # build (default env: esp32-c3-mini)
~/.platformio/penv/bin/pio run -t upload      # flash over USB
~/.platformio/penv/bin/pio device monitor     # serial monitor (115200 baud)
```

If upload fails on the Super Mini (no auto-reset circuit): hold BOOT, press and release RESET, release BOOT, retry upload.

## Architecture

```
Sowel plugin somfy-rts <--MQTT--> mosquitto <--MQTT--> ESP32 + CC1101 <--RF 433.42--> Somfy shutters
```

MQTT topics (prefix `somfy2mqtt`):
- `somfy2mqtt/<remote_id>/set`: command (`up` / `down` / `stop` / `program`)
- `somfy2mqtt/<remote_id>/state`: retained, last command sent (RTS is unidirectional, no real feedback)
- `somfy2mqtt/<remote_id>/rolling_code`: retained, current counter (backup for restore after flash)

Pairing: web UI on the ESP (AP mode at first boot or via a long-press on a GPIO). A "virtual remote" = `(remote_id on 24 bits, rolling_code)` stored in NVS.

## Libraries

- [Legion2/Somfy_Remote_Lib](https://github.com/Legion2/Somfy_Remote_Lib) — Somfy RTS frames (maintained fork of Nickduino)
- [LSatan/SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib) — CC1101 SPI driver
- To add when needed: `knolleary/PubSubClient`, `bblanchon/ArduinoJson`, `esp32async/AsyncTCP` + `esp32async/ESPAsyncWebServer`

## Gotchas

1. **CC1101 crystal 26 vs 27 MHz** — `ELECHOUSE_cc1101.setMHZ(433.42)` assumes 26 MHz. We are good here (verified on the module). With a 27 MHz crystal, `setClb()` is required to recalibrate.
2. **Rolling code** — losing the counter means re-pairing every motor physically (PROG button on the back of the shutter). Persist in NVS and back up over MQTT on the Sowel side.
3. **RTS is unidirectional** — no feedback from the motor. "Position" will always be a time-based estimate on the Sowel side.
4. **Antenna is mandatory** — without the 17.3 cm wire, range is under 1 m.

## Code style — minimalist C++

Sober C++17, never plain C (the Arduino/ESP ecosystem is C++; writing C means wrapping every library).

**Rules:**

- **One feature = one module**: `include/<name>.h` + `src/<name>.cpp`, wrapped in a `namespace` of the same name (`rf`, `mqtt`, `wifi`, `nvs_store`, `web_ui`).
- **Minimal `.h`**: only the public API and the types it needs. Internal state stays `static` in the `.cpp`, not exposed.
- **Free functions > classes**. Use a class only when an object has a real identity or lifetime (e.g. a Somfy virtual `Remote`, an MQTT client). No inheritance unless forced by a library.
- **`enum class`** for every state and command — never `int` or `#define` constants.
- **`constexpr`** for any value known at compile time (pins, timeouts, topics, lengths).
- **No dynamic allocation in steady state**. Buffers are `static` or on the stack. `new`/`delete` forbidden inside `loop()`.
- **No exceptions** (disabled by default on arduino-esp32). Errors are returned via `bool`, `std::optional<T>`, or an explicit return code.
- **No operator overloading**, no business-logic templates, no dynamic polymorphism.

Reference example:

```cpp
// include/rf.h
#pragma once
#include <cstdint>

namespace rf {
  bool init();                                   // false if CC1101 does not respond
  bool send(uint32_t remote_id,
            uint16_t rolling_code,
            uint8_t  button);
  uint8_t cc1101_version();
}
```

```cpp
// src/rf.cpp
#include "rf.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include "config.h"

namespace rf {
  static bool s_ready = false;                   // module state, kept out of the .h

  bool init() {
    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO,
                               CC1101_MOSI, CC1101_CSN);
    if (!ELECHOUSE_cc1101.getCC1101()) return false;
    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setMHZ(SOMFY_FREQ_MHZ);
    s_ready = true;
    return true;
  }
}
```

## Code comments — Doxygen

Every header, every public symbol gets a Doxygen block. Implementation files keep brief inline comments only for the **why** of non-obvious choices.

**File header** (every `.h` and `.cpp`):

```cpp
/**
 * @file <filename>
 * @brief <one-liner>
 *
 * <Optional, 2-3 lines max if the file's role deserves more context.>
 */
```

**Namespace block** (only in `.h`):

```cpp
/**
 * @namespace logger
 * @brief Lightweight serial logging with [tag] prefix.
 *
 * Wraps Serial.printf. No level filtering yet — info/warn/err share
 * the same output format. The function choice documents intent and
 * reserves the API for future level filtering.
 */
namespace logger {
```

**Public function** (in the `.h`):

```cpp
/**
 * @brief Emit an info-level log line.
 * @param tag  Module tag prefixed in brackets (e.g. "wifi", "boot").
 * @param fmt  printf-style format string.
 * @param ...  printf-style arguments.
 */
void info(const char* tag, const char* fmt, ...);
```

**Implementation `.cpp`**: no Doxygen block duplication. Inline comments only when the **why** is not obvious from the code:

```cpp
// WiFi.setAutoReconnect(true) covers the retry — no need for a custom state machine.
WiFi.setAutoReconnect(true);
```

**Allowed Doxygen tags**: `@file`, `@brief`, `@namespace`, `@param`, `@return`, `@note`, `@warning`, `@see`. No `@author` per function. No `@date` (`git blame` does the job).

## Tooling

### Code formatting — clang-format

`.clang-format` defines the project style (LLVM base, 2-space indent, 100-column limit). Run before commit:

```bash
clang-format -i src/*.cpp include/*.h test/**/*.cpp
```

`.editorconfig` covers IDE-level basics (charset, EOL, trailing whitespace).

### Static analysis — cppcheck

Configured in `platformio.ini` as `check_tool = cppcheck`. Run locally:

```bash
~/.platformio/penv/bin/pio check -d .
```

Severity threshold: `high, medium`. Third-party packages are skipped.

### Unit tests — Unity (PlatformIO `pio test`)

Pure-logic modules (anything that does not touch hardware) get native tests under `test/`:

```bash
~/.platformio/penv/bin/pio test -d . -e native
```

Logger and wifi are not unit-tested (they wrap Serial / WiFi.h). NVS schema, rolling code arithmetic, MQTT topic parsing, etc. **must** have native tests.

### CI — GitHub Actions

`.github/workflows/ci.yml` runs on every push and PR: build, `pio check`, `pio test -e native`. `.github/workflows/codeql.yml` runs a weekly security scan and on PRs. `.github/dependabot.yml` keeps Action versions up to date.

PRs cannot be merged unless CI is green. The check is the contract, not the suggestion.

## Commit convention

Conventional Commits. Useful scopes: `rf`, `mqtt`, `wifi`, `web`, `nvs`, `core`, `build`, `ci`, `docs`, `spec`, `test`.

No `Co-Authored-By` lines.

## Skills

- `somfy-iterate` — dev workflow (short spec → branch → code → flash & HW test → PR). See [.claude/skills/somfy-iterate/SKILL.md](.claude/skills/somfy-iterate/SKILL.md).

## Specs

Every non-trivial feature or fix lives in `specs/XXX-<name>/` (spec.md / architecture.md / plan.md, each short). No code commit without a matching spec.
