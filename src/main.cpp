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

/// MQTT command trampoline: the orchestrator's handle_command takes an extra
/// optional `repeat_override` argument, so its function pointer no longer
/// matches `mqtt::CommandHandler` (default args are not part of the type).
/// This wrapper restores the expected 2-arg signature.
static void on_mqtt_command(uint32_t remote_id, mqtt::Command cmd) {
  orchestrator::handle_command(remote_id, cmd);
}

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
  mqtt::init(on_mqtt_command);
  // Give WiFi up to 15 s to get an IP so the web_ui log can show the real
  // address. The cold-boot path scans + connects in ~3-8 s on a healthy board;
  // we allow more headroom for boards with a degraded RF chain.
  // Calling wifi::loop() here lets the sticky-bad-BSSID recovery kick in
  // during the boot grace window (otherwise the rescan would only trigger
  // once setup() returns and the main loop starts).
  for (int i = 0; i < 150 && !wifi::is_connected(); ++i) {
    wifi::loop();
    delay(100);
  }
  web_ui::init();
}

void loop() {
  wifi::loop();
  mqtt::loop();
  // Drain at most one queued web-UI command per tick. RF emissions take
  // 360 ms (regular) to 7 s (Erase PROG 7 s) -- doing one per tick keeps
  // the WiFi/MQTT loops responsive in between.
  orchestrator::process_queue();
  delay(20);
}
