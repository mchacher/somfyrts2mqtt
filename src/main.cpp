/**
 * @file main.cpp
 * @brief Firmware entry point. Boots every module and wires the dispatch chain.
 */
#include <Arduino.h>
#include "logger.h"
#include "mqtt.h"
#include "nvs_store.h"
#include "orchestrator.h"
#include "rf.h"
#include "secrets.h"
#include "web_ui.h"
#include "wifi_manager.h"

/**
 * @brief Seed the broker config from secrets.h if NVS does not have one yet.
 *
 * One-shot helper used the very first time a board boots. After that, the
 * web UI is the canonical place to edit the broker config.
 */
static void bootstrap_mqtt_from_secrets() {
  if (!nvs_store::ready()) return;
  const nvs_store::MqttConfig current = nvs_store::get_mqtt();
  if (!current.host.empty()) return;
  nvs_store::MqttConfig seed;
  seed.host = MQTT_BROKER_HOST;
  seed.port = MQTT_BROKER_PORT;
  seed.user = MQTT_BROKER_USER;
  seed.pass = MQTT_BROKER_PASS;
  if (nvs_store::set_mqtt(seed)) {
    logger::info("boot", "seeded MQTT config from secrets.h host=%s port=%u",
                 seed.host.c_str(), seed.port);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  logger::info("boot", "hello somfyrts2mqtt v%s", FW_VERSION);
  nvs_store::init();
  bootstrap_mqtt_from_secrets();
  rf::init();
  wifi::init();
  mqtt::init(orchestrator::handle_command);
  // Give WiFi a few seconds to get an IP so the web_ui log can show it.
  for (int i = 0; i < 50 && !wifi::is_connected(); ++i) delay(100);
  web_ui::init();
}

void loop() {
  wifi::loop();
  mqtt::loop();
  delay(20);
}
