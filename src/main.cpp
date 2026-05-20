/**
 * @file main.cpp
 * @brief Firmware entry point. Boots the logger and the WiFi manager.
 */
#include <Arduino.h>
#include "logger.h"
#include "wifi_manager.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  logger::info("boot", "hello somfyrts2mqtt");
  wifi::init();
}

void loop() {
  wifi::loop();
  delay(100);
}
