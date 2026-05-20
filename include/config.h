/**
 * @file config.h
 * @brief Project-wide configuration constants (board pinout, RF, MQTT).
 *
 * The CC1101 pinout block is selected at compile time from the target chip.
 * Add a new `#elif CONFIG_IDF_TARGET_*` block to support another variant.
 */
#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32C3)
  // --- ESP32-C3 Super Mini ---
  // Strapping pins to avoid: GPIO 2, 8, 9.
  #define CC1101_SCK   4
  #define CC1101_MISO  5
  #define CC1101_MOSI  6
  #define CC1101_CSN   7
  #define CC1101_GDO0  10   ///< TX data line (used by Somfy_Remote_Lib).
  #define CC1101_GDO2  3    ///< RX (optional, for sniffing existing remotes).
#elif defined(CONFIG_IDF_TARGET_ESP32)
  // --- ESP32 WROOM (NodeMCU-32S / DevKit-32) ---
  // VSPI default pins. Strapping pins to avoid: GPIO 0, 2, 5, 12, 15.
  #define CC1101_SCK   18
  #define CC1101_MISO  19
  #define CC1101_MOSI  23
  #define CC1101_CSN   21
  #define CC1101_GDO0  22   ///< TX data line.
  #define CC1101_GDO2  17   ///< RX (optional). Conflicts with UART2 TX if ever used.
#elif defined(ARDUINO_ARCH_ESP32)
  // cppcheck enumerates preprocessor configs without the CONFIG_IDF_TARGET_*
  // macros (they come from sdkconfig.h, not from build_flags), so it would
  // otherwise hit this branch and flag the #error as a real defect. The
  // suppression below is safe because the framework always defines one of
  // the two CONFIG_IDF_TARGET_* macros in a real compile.
  // cppcheck-suppress preprocessorErrorDirective
  #error "Unsupported ESP32 variant -- add a CC1101 pinout block in include/config.h"
// Else: native / host build — no CC1101 pins needed (tests cover pure logic only).
#endif

/// Somfy RTS carrier frequency.
#define SOMFY_FREQ_MHZ 433.42f

// MQTT
#define MQTT_TOPIC_PREFIX  "somfy2mqtt"
#define MQTT_CLIENT_PREFIX "somfy2mqtt-"

/// SSID exposed by the ESP in AP fallback mode (captive portal for first-time setup).
#define SETUP_AP_SSID "somfy2mqtt-setup"
