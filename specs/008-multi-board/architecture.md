# Architecture 008

## Touched files

| File | Change |
|---|---|
| `platformio.ini` | Add `[firmware_base]` shared section + `[env:esp32-wroom]` env; existing `[env:esp32-c3-mini]` `extends` the base |
| `include/config.h` | `#if CONFIG_IDF_TARGET_ESP32C3` ... `#elif CONFIG_IDF_TARGET_ESP32` ... `#else #error` |
| `.github/workflows/ci.yml` | Matrix on the build/check job: `[esp32-c3-mini, esp32-wroom]` |
| `.github/workflows/codeql.yml` | Build one env (C3 by convention) — CodeQL doesn't need to cross-build both targets |
| `CLAUDE.md` | Two-column pinout table |

No code change in modules (logger / nvs_store / mqtt / orchestrator / rf / web_ui / wifi_manager) — they're already portable.

## Pinout — ESP32 WROOM (NodeMCU-32S / DevKit)

VSPI standard pins, no strapping pin used:

| CC1101 module | NodeMCU-32S | Strapping? |
|---|---|---|
| VCC | 3V3 | — |
| GND | GND | — |
| SCK | GPIO18 | no |
| MISO | GPIO19 | no |
| MOSI | GPIO23 | no |
| CSN | GPIO21 | no |
| GDO0 | GPIO22 | no |
| GDO2 | GPIO17 (TX2) | no — but conflicts with UART2 if ever used |

Strapping pins to avoid on WROOM: GPIO 0, 2, 5, 12, 15.

## `platformio.ini` layout

```ini
[platformio]
default_envs = esp32-c3-mini

[common]
build_flags = -DFW_VERSION=\"0.1.0\" -Wall

[firmware_base]
platform = espressif32
framework = arduino
monitor_speed = 115200
upload_speed = 921600
board_build.filesystem = littlefs
board_build.partitions = min_spiffs.csv
build_flags =
    ${common.build_flags}
    -DCORE_DEBUG_LEVEL=3
    -DASYNCWEBSERVER_REGEX
check_tool = cppcheck
check_severity = high, medium
check_skip_packages = yes
check_src_filters = +<src/> +<include/>
check_flags = ...
lib_deps =
    https://github.com/Legion2/Somfy_Remote_Lib.git
    https://github.com/LSatan/SmartRC-CC1101-Driver-Lib.git
    knolleary/PubSubClient@^2.8
    bblanchon/ArduinoJson@^7.0.4
    esp32async/AsyncTCP@^3.4.9
    esp32async/ESPAsyncWebServer@^3.11.0
test_ignore = test_*

[env:esp32-c3-mini]
extends = firmware_base
board = esp32-c3-devkitm-1
build_flags =
    ${firmware_base.build_flags}
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1

[env:esp32-wroom]
extends = firmware_base
board = esp32dev

[env:native]
platform = native
build_flags = ${common.build_flags} -std=gnu++17
test_framework = unity
```

## `config.h` layout

```cpp
#if CONFIG_IDF_TARGET_ESP32C3
  // ESP32-C3 Super Mini. Strapping pins to avoid: GPIO 2, 8, 9.
  #define CC1101_SCK   4
  #define CC1101_MISO  5
  #define CC1101_MOSI  6
  #define CC1101_CSN   7
  #define CC1101_GDO0  10
  #define CC1101_GDO2  3
#elif CONFIG_IDF_TARGET_ESP32
  // ESP32 WROOM (NodeMCU-32S / DevKit). Strapping pins to avoid: GPIO 0, 2, 5, 12, 15.
  #define CC1101_SCK   18
  #define CC1101_MISO  19
  #define CC1101_MOSI  23
  #define CC1101_CSN   21
  #define CC1101_GDO0  22
  #define CC1101_GDO2  17
#else
  #error "Unsupported target chip — add a CC1101 pinout block in include/config.h"
#endif
```

The rest of `config.h` (`SOMFY_FREQ_MHZ`, MQTT prefixes, AP SSID) stays unconditional.

## CI matrix

```yaml
jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        env: [esp32-c3-mini, esp32-wroom]
    runs-on: ubuntu-latest
    steps:
      - ...
      - name: Build firmware
        run: pio run -e ${{ matrix.env }}
      - name: Static analysis
        run: pio check -e ${{ matrix.env }} --fail-on-defect=high --fail-on-defect=medium
      - name: Native unit tests
        if: matrix.env == 'esp32-c3-mini'
        run: pio test -e native
```

Native tests run once (under the C3 job) — they don't depend on the firmware env.
