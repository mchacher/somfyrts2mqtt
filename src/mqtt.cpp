/**
 * @file mqtt.cpp
 * @brief PubSubClient-backed implementation of mqtt. See mqtt.h.
 */
#include "mqtt.h"

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "config.h"
#include "logger.h"
#include "nvs_store.h"
#include "wifi_manager.h"

namespace mqtt {

  static WiFiClient    s_wifi_client;
  static PubSubClient  s_client(s_wifi_client);
  static CommandHandler s_handler          = nullptr;
  static unsigned long  s_last_attempt_ms  = 0;
  static bool           s_was_connected    = false;
  static char           s_client_id[24]    = {0};

  // --- helpers ---

  /// Build a stable client id from the prefix + the last 3 MAC bytes.
  static void build_client_id() {
    uint64_t mac = ESP.getEfuseMac();
    std::snprintf(s_client_id, sizeof(s_client_id),
                  "%s%02X%02X%02X",
                  MQTT_CLIENT_PREFIX,
                  static_cast<unsigned>((mac >> 16) & 0xFF),
                  static_cast<unsigned>((mac >> 8) & 0xFF),
                  static_cast<unsigned>(mac & 0xFF));
  }

  static void on_message(char* topic, uint8_t* payload, unsigned int len) {
    uint32_t remote_id = 0;
    if (!parse_set_topic(topic, remote_id)) {
      logger::warn("mqtt", "dropped: bad topic '%s'", topic);
      return;
    }
    const Command cmd = parse_command(reinterpret_cast<const char*>(payload), len);
    if (cmd == Command::Invalid) {
      logger::warn("mqtt", "dropped: bad payload on '%s' (%u bytes)", topic, len);
      return;
    }
    logger::info("mqtt", "cmd id=%06X cmd=%s",
                 static_cast<unsigned>(remote_id), command_to_str(cmd));
    if (s_handler != nullptr) {
      s_handler(remote_id, cmd);
    }
  }

  static bool try_connect() {
    const nvs_store::MqttConfig cfg = nvs_store::get_mqtt();
    if (cfg.host.empty() || cfg.port == 0) {
      logger::warn("mqtt", "no broker config in NVS");
      return false;
    }
    s_client.setServer(cfg.host.c_str(), cfg.port);
    s_client.setCallback(on_message);

    logger::info("mqtt", "connecting host=%s port=%u id=%s",
                 cfg.host.c_str(), cfg.port, s_client_id);

    const char* user = cfg.user.empty() ? nullptr : cfg.user.c_str();
    const char* pass = cfg.pass.empty() ? nullptr : cfg.pass.c_str();

    const bool ok = s_client.connect(
        s_client_id, user, pass,
        BRIDGE_STATE_TOPIC,                 // will topic
        0,                                  // will QoS
        true,                               // will retain
        BRIDGE_STATE_OFFLINE);              // will message

    if (!ok) {
      logger::warn("mqtt", "connect failed rc=%d", s_client.state());
      return false;
    }

    s_client.publish(BRIDGE_STATE_TOPIC, BRIDGE_STATE_ONLINE, true);
    const char* sub = MQTT_TOPIC_PREFIX "/+/set";
    s_client.subscribe(sub);
    logger::info("mqtt", "connected, subscribed %s", sub);
    return true;
  }

  // --- public API ---

  void init(CommandHandler handler) {
    s_handler = handler;
    build_client_id();
    s_client.setBufferSize(256);          // headroom for retained state payloads
  }

  void loop() {
    if (!wifi::is_connected()) return;

    if (!s_client.connected()) {
      if (s_was_connected) {
        logger::warn("mqtt", "disconnected, will retry");
        s_was_connected = false;
      }
      const unsigned long now = millis();
      if (s_last_attempt_ms == 0 || now - s_last_attempt_ms >= RECONNECT_INTERVAL_MS) {
        s_last_attempt_ms = now;
        if (try_connect()) {
          s_was_connected = true;
        }
      }
      return;
    }
    s_was_connected = true;
    s_client.loop();
  }

  bool is_connected() {
    return s_client.connected();
  }

  bool publish_state(uint32_t remote_id, Command last_cmd) {
    if (!s_client.connected()) return false;
    char topic[24];
    build_state_topic(remote_id, topic);
    return s_client.publish(topic, command_to_str(last_cmd), true);
  }

  bool publish_rolling_code(uint32_t remote_id, uint16_t code) {
    if (!s_client.connected()) return false;
    char topic[32];
    build_rolling_code_topic(remote_id, topic);
    char payload[8];
    std::snprintf(payload, sizeof(payload), "%u", code);
    return s_client.publish(topic, payload, true);
  }

}
