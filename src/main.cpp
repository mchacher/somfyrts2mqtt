/**
 * @file main.cpp
 * @brief Firmware entry point. Boots every module and wires the dispatch chain.
 */
#include <Arduino.h>
#include <esp_system.h>
#include "logger.h"
#include "mqtt.h"
#include "nvs_store.h"
#include "orchestrator.h"
#include "rf.h"
#include "secrets.h"
#include "web_ui.h"
#include "wifi_manager.h"

/// Decode the last reset reason into a short tag for the boot log.
/// Critical for diagnosing reset loops : BROWNOUT vs TASK_WDT vs PANIC vs
/// POWERON each point at different root causes (LDO marginal, blocking
/// callback, exception, or fresh boot).
static const char* reset_reason_tag(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";        // external pin (RESET button)
    case ESP_RST_SW:        return "SW";         // ESP.restart()
    case ESP_RST_PANIC:     return "PANIC";      // exception / abort()
    case ESP_RST_INT_WDT:   return "INT_WDT";    // interrupt watchdog
    case ESP_RST_TASK_WDT:  return "TASK_WDT";   // task watchdog
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";   // LDO / supply marginal
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "?";
  }
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
#if defined(WAIT_FOR_SERIAL)
  // Opt-in dev aid for native-USB boards (C3/S3): the USB-Serial/JTAG CDC
  // re-enumerates on the host after a reset, so the boot log is otherwise lost
  // before the terminal re-attaches. Off by default -- a fixed multi-second
  // wait would slow every headless production boot (power-loss recovery).
  // Build with `-DWAIT_FOR_SERIAL` to capture boot output over USB.
  for (unsigned long t = millis(); !Serial && millis() - t < 3000UL;) delay(10);
#endif
  delay(200);
  Serial.println();
  const esp_reset_reason_t rr = esp_reset_reason();
  logger::info("boot", "hello somfyrts2mqtt v%s reset_reason=%s(%d)",
               FW_VERSION, reset_reason_tag(rr), static_cast<int>(rr));
  nvs_store::init();
  bootstrap_mqtt_from_secrets();
  rf::init();
  wifi::init();
  mqtt::init();
  orchestrator::init_runtimes();
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

  // 1 Hz position tick (iter 014). Updates live position estimates for
  // any shutter currently in motion, enqueues Stop on intermediate-target
  // reached, and triggers SENSOR telemetry.
  static unsigned long s_last_tick_ms = 0;
  const unsigned long now = millis();
  if (now - s_last_tick_ms >= 1000UL) {
    s_last_tick_ms = now;
    orchestrator::tick(now);
  }

  delay(20);
}
