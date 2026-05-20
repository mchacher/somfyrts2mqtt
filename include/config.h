#pragma once

// Board: ESP32-C3 Super Mini
// CC1101 wiring matches the SPI pins silkscreened on the board.
// Strapping pins to avoid: GPIO2, GPIO8, GPIO9.
#define CC1101_SCK   4
#define CC1101_MISO  5
#define CC1101_MOSI  6
#define CC1101_CSN   7
#define CC1101_GDO0  10  // TX data line (used by Somfy_Remote_Lib)
#define CC1101_GDO2  3   // RX (optional, for sniffing existing remotes)

// Somfy RTS carrier frequency
#define SOMFY_FREQ_MHZ 433.42f

// MQTT
#define MQTT_TOPIC_PREFIX  "somfy2mqtt"
#define MQTT_CLIENT_PREFIX "somfy2mqtt-"

// Default WiFi AP (captive portal for first-time setup)
#define SETUP_AP_SSID "somfy2mqtt-setup"
