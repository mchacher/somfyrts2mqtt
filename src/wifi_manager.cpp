/**
 * @file wifi_manager.cpp
 * @brief WiFi manager implementation. See wifi_manager.h for the API.
 */
#include "wifi_manager.h"
#include <WiFi.h>
#include "logger.h"
#include "secrets.h"

namespace wifi {

  static void on_event(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        logger::info("wifi", "connected ip=%s",
                     WiFi.localIP().toString().c_str());
        break;
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        logger::warn("wifi", "disconnected reason=%d",
                     info.wifi_sta_disconnected.reason);
        break;
      default:
        break;
    }
  }

  void init() {
    WiFi.mode(WIFI_STA);
    WiFi.onEvent(on_event);
    // The ESP32 core covers retry on its own — no need for a custom state machine.
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    logger::info("wifi", "connecting ssid=%s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  void loop() {
    // No-op for now. Reserved for future watchdog or status reporting.
  }

  bool is_connected() {
    return WiFi.status() == WL_CONNECTED;
  }

}
