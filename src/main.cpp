/**
 * @file main.cpp
 * @brief Firmware entry point. Boots logger, NVS, WiFi, and MQTT.
 */
#include <Arduino.h>
#include "logger.h"
#include "mqtt.h"
#include "nvs_store.h"
#include "secrets.h"
#include "wifi_manager.h"

/// Temporary command handler until iter 004 wires the orchestrator.
static void default_command_handler(uint32_t remote_id, mqtt::Command cmd) {
  logger::info("orch", "stub received id=%06X cmd=%s",
               static_cast<unsigned>(remote_id), mqtt::command_to_str(cmd));
}

/**
 * @brief Seed the broker config from secrets.h if NVS does not have one yet.
 *
 * One-shot helper used until the web UI (iter 007) provides runtime config.
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
  logger::info("boot", "hello somfyrts2mqtt");
  nvs_store::init();
  bootstrap_mqtt_from_secrets();
  wifi::init();
  mqtt::init(default_command_handler);
}

void loop() {
  wifi::loop();
  mqtt::loop();
  delay(20);
}
