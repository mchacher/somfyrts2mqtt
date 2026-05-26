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
#elif defined(ARDUINO_ARCH_ESP32)
  // cppcheck enumerates preprocessor configs without the CONFIG_IDF_TARGET_*
  // macros (they come from sdkconfig.h, not from build_flags), so it would
  // otherwise hit this branch and flag the #error as a real defect. The
  // suppression below is safe because the framework always defines
  // CONFIG_IDF_TARGET_ESP32C3 in a real compile of the only supported board.
  // cppcheck-suppress preprocessorErrorDirective
  #error "Unsupported ESP32 variant -- only the ESP32-C3 Super Mini is supported (spec 020)"
// Else: native / host build — no CC1101 pins needed (tests cover pure logic only).
#endif

/// Somfy RTS carrier frequency.
#define SOMFY_FREQ_MHZ 433.42f

// MQTT
#define MQTT_TOPIC_PREFIX  "somfy2mqtt"
#define MQTT_CLIENT_PREFIX "somfy2mqtt-"

// --- WiFi commissioning (iter 015) ---
//
// On a fresh box (NVS empty) OR after 4 rapid power-cycles (< 5 s of stable
// uptime each), the firmware starts a SoftAP and serves a tzapu/WiFiManager
// captive portal that lets the user enter SSID + password. Saved creds land
// in NVS via nvs_store::set_wifi_creds() then the box reboots into STA.

/// Prefix for the SoftAP SSID. The 6-hex chipId is appended at runtime so
/// distinct boxes on the same site advertise distinct AP names.
#define WIFI_AP_SSID_PREFIX "somfyrts2mqtt-"

/// Uptime threshold (ms) at which the in-RAM boot counter is reset to 0.
/// Power-cycling the box faster than this leaves the counter elevated.
#define WIFI_BOOT_STABLE_MS 5000U

/// Boot-counter value that triggers AP recovery mode. 4 quick power-cycles
/// hit the threshold ; a 5th would be a no-op (the counter saturates).
#define WIFI_BOOT_AP_THRESHOLD 4U

/// Maximum time (seconds) the captive portal stays up without a successful
/// /save. After that, ESP.restart() to attempt STA again with whatever
/// creds remain in NVS. Prevents the box from being stuck in AP forever
/// after an accidental trigger.
#define WIFI_AP_TIMEOUT_S (5U * 60U)
