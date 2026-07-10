/**
 * @file mqtt.cpp
 * @brief PubSubClient-backed implementation of mqtt -- iter 014
 *        Tasmota Shutter subset. See mqtt.h.
 */
#include "mqtt.h"

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "config.h"
#include "device_profile.h"
#include "logger.h"
#include "nvs_store.h"
#include "orchestrator.h"
#include "wifi_manager.h"

namespace mqtt {

  static WiFiClient    s_wifi_client;
  static PubSubClient  s_client(s_wifi_client);
  static unsigned long s_last_attempt_ms = 0;
  static unsigned long s_retry_ms        = RECONNECT_BASE_MS;
  static bool          s_was_connected   = false;
  static char          s_client_id[32]   = {0};
  static char          s_root_topic[nvs_store::MAX_TOPIC_LEN + 1] = {0};

  // --- helpers ---

  /// Default root topic when the user has not set `mqtt.topic` in NVS.
  /// Short and friendly for the common case (single bridge per broker).
  /// Multi-bridge setups must give each instance a distinct topic via
  /// the web UI (otherwise the shared client_id triggers a connect /
  /// kick / reconnect loop on the broker).
  static void compute_default_root(char* out, size_t cap) {
    std::snprintf(out, cap, "somfyrts2mqtt");
  }

  static void refresh_root_topic() {
    const nvs_store::MqttConfig cfg = nvs_store::get_mqtt();
    if (!cfg.topic.empty() &&
        cfg.topic.size() < sizeof(s_root_topic) &&
        nvs_store::is_valid_topic(cfg.topic)) {
      std::strncpy(s_root_topic, cfg.topic.c_str(), sizeof(s_root_topic) - 1);
      s_root_topic[sizeof(s_root_topic) - 1] = '\0';
    } else {
      compute_default_root(s_root_topic, sizeof(s_root_topic));
    }
  }

  static void build_client_id() {
    // Use the same naming as the root topic so brokers / tools that key
    // on client_id show the Tasmota-style device name directly.
    refresh_root_topic();
    std::strncpy(s_client_id, s_root_topic, sizeof(s_client_id) - 1);
    s_client_id[sizeof(s_client_id) - 1] = '\0';
  }

  /// Look up a remote by name. Linear scan, max 16 remotes.
  static bool find_remote_by_name(const char* name, nvs_store::Remote& out) {
    nvs_store::Remote buf[nvs_store::MAX_REMOTES];
    const size_t n = nvs_store::list_remotes(buf, nvs_store::MAX_REMOTES);
    for (size_t i = 0; i < n; ++i) {
      if (buf[i].name == name) {
        out = buf[i];
        return true;
      }
    }
    return false;
  }

  // --- incoming message dispatch ---

  // Build "cmnd/<root>/Status" into a stack buffer for prefix matching.
  static size_t build_status_topic(char* out, size_t cap) {
    return static_cast<size_t>(std::snprintf(out, cap, "cmnd/%s/Status", s_root_topic));
  }

  static void on_message(char* topic, uint8_t* payload, unsigned int len) {
    // Bridge-wide "Status" verb : republish the retained SENSOR snapshot.
    // Cheap prefix check before falling through to the per-remote parser
    // (which expects 4 segments and would reject this 3-segment topic).
    char status_topic[16 + nvs_store::MAX_TOPIC_LEN];
    build_status_topic(status_topic, sizeof(status_topic));
    if (std::strcmp(topic, status_topic) == 0) {
      (void)payload;
      (void)len;
      logger::info("mqtt", "cmnd Status -> republishing SENSOR");
      publish_sensor_aggregated();
      return;
    }

    char name[MAX_NAME_LEN + 1];
    char verb[MAX_VERB_LEN + 1];
    if (!parse_cmnd_topic(topic, s_root_topic,
                          name, sizeof(name),
                          verb, sizeof(verb))) {
      logger::warn("mqtt", "dropped : bad topic '%s'", topic);
      return;
    }

    nvs_store::Remote remote;
    if (!find_remote_by_name(name, remote)) {
      logger::warn("mqtt", "dropped : no remote named '%s'", name);
      return;
    }

    // Copy payload into a NUL-terminated stack buffer for atoi / atof.
    char buf[MAX_CMD_PAYLOAD_LEN + 1];
    const size_t copy_len = (len < sizeof(buf) - 1)
                            ? len
                            : sizeof(buf) - 1;
    std::memcpy(buf, payload, copy_len);
    buf[copy_len] = '\0';

    logger::info("mqtt", "cmnd %s/%s payload='%s'", name, verb, buf);

    if (std::strcmp(verb, "Open") == 0) {
      orchestrator::handle_command(remote.id, Command::Up);
    } else if (std::strcmp(verb, "Close") == 0) {
      orchestrator::handle_command(remote.id, Command::Down);
    } else if (std::strcmp(verb, "Stop") == 0) {
      orchestrator::handle_command(remote.id, Command::Stop);
    } else if (std::strcmp(verb, "Toggle") == 0) {
      // iter 022 : Gate single-button cycle. Emits the remote's configured
      // toggle button; a no-op label for a shutter (still emits its Up).
      orchestrator::handle_command(remote.id, Command::Toggle);
    } else if (std::strcmp(verb, "Position") == 0) {
      const int p = std::atoi(buf);
      orchestrator::set_position(remote.id,
                                 static_cast<uint8_t>(p < 0 ? 0 : (p > 100 ? 100 : p)));
    } else if (std::strcmp(verb, "OpenDuration") == 0) {
      const float s = static_cast<float>(std::atof(buf));
      const uint32_t ms = (s < 0.0f) ? 0u : static_cast<uint32_t>(s * 1000.0f);
      orchestrator::set_open_duration(remote.id, ms);
    } else if (std::strcmp(verb, "CloseDuration") == 0) {
      const float s = static_cast<float>(std::atof(buf));
      const uint32_t ms = (s < 0.0f) ? 0u : static_cast<uint32_t>(s * 1000.0f);
      orchestrator::set_close_duration(remote.id, ms);
    } else if (std::strcmp(verb, "SetPosition") == 0) {
      const int p = std::atoi(buf);
      orchestrator::set_calibration_position(
          remote.id,
          static_cast<uint8_t>(p < 0 ? 0 : (p > 100 ? 100 : p)));
    } else {
      logger::warn("mqtt", "dropped : unknown verb '%s'", verb);
    }
  }

  // --- connect / subscribe ---

  static bool try_connect() {
    refresh_root_topic();
    const nvs_store::MqttConfig cfg = nvs_store::get_mqtt();
    if (cfg.host.empty() || cfg.port == 0) {
      logger::warn("mqtt", "no broker config in NVS");
      return false;
    }
    s_client.setServer(cfg.host.c_str(), cfg.port);
    s_client.setCallback(on_message);

    logger::info("mqtt", "connecting host=%s port=%u id=%s root=%s",
                 cfg.host.c_str(), cfg.port, s_client_id, s_root_topic);

    const char* user = cfg.user.empty() ? nullptr : cfg.user.c_str();
    const char* pass = cfg.pass.empty() ? nullptr : cfg.pass.c_str();

    char lwt_topic[16 + nvs_store::MAX_TOPIC_LEN];
    build_lwt_topic(s_root_topic, lwt_topic, sizeof(lwt_topic));

    const bool ok = s_client.connect(
        s_client_id, user, pass,
        lwt_topic, /*will_qos*/ 0, /*will_retain*/ true, LWT_OFFLINE);

    if (!ok) {
      const int rc = s_client.state();
      logger::warn("mqtt", "connect failed rc=%d (%s)", rc, state_str(rc));
      return false;
    }

    // Online presence (retained, mirrors the LWT topic).
    s_client.publish(lwt_topic, LWT_ONLINE, true);

    // Per-remote commands : `cmnd/<root>/+/+` matches every <name>/<verb>.
    char sub[16 + nvs_store::MAX_TOPIC_LEN];
    build_cmnd_subscription(s_root_topic, sub, sizeof(sub));
    s_client.subscribe(sub);

    // Bridge-wide commands : `cmnd/<root>/Status` (single fixed topic). Used
    // by subscribers that want to force a fresh SENSOR publish on demand.
    char status_sub[16 + nvs_store::MAX_TOPIC_LEN];
    build_status_topic(status_sub, sizeof(status_sub));
    s_client.subscribe(status_sub);

    logger::info("mqtt", "connected, subscribed %s + %s", sub, status_sub);

    // Republish the aggregated SENSOR so a fresh subscriber sees current state.
    publish_sensor_aggregated();

    return true;
  }

  // --- public API ---

  void init() {
    build_client_id();
    s_client.setBufferSize(512);               // headroom for aggregated SENSOR
    s_client.setSocketTimeout(SOCKET_TIMEOUT_S);
    s_client.setKeepAlive(KEEPALIVE_S);
  }

  void loop() {
    if (!wifi::is_connected()) return;

    if (!s_client.connected()) {
      if (s_was_connected) {
        logger::warn("mqtt", "disconnected, will retry");
        s_was_connected = false;
      }
      const unsigned long now = millis();
      if (s_last_attempt_ms == 0 || now - s_last_attempt_ms >= s_retry_ms) {
        s_last_attempt_ms = now;
        if (try_connect()) {
          s_was_connected = true;
          s_retry_ms = RECONNECT_BASE_MS;
        } else {
          const unsigned long next = s_retry_ms * 2UL;
          s_retry_ms = (next > RECONNECT_MAX_MS) ? RECONNECT_MAX_MS : next;
          logger::warn("mqtt", "next attempt in %lus", s_retry_ms / 1000UL);
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

  void disconnect() {
    if (s_client.connected()) {
      // Publish Offline retained before tearing down, so subscribers know
      // immediately rather than waiting for the broker's LWT trigger.
      char lwt_topic[16 + nvs_store::MAX_TOPIC_LEN];
      build_lwt_topic(s_root_topic, lwt_topic, sizeof(lwt_topic));
      s_client.publish(lwt_topic, LWT_OFFLINE, true);
      s_client.disconnect();
      logger::info("mqtt", "disconnect requested");
    }
    s_last_attempt_ms = 0;
    s_retry_ms        = RECONNECT_BASE_MS;
  }

  const char* get_root_topic() {
    if (s_root_topic[0] == '\0') refresh_root_topic();
    return s_root_topic;
  }

  // --- publish helpers ---

  bool publish_shutter_state(const char* name,
                             uint8_t position, int8_t direction, uint8_t target,
                             uint8_t device_type) {
    if (!s_client.connected() || name == nullptr) return false;
    char topic[16 + nvs_store::MAX_TOPIC_LEN + nvs_store::MAX_NAME_LEN];
    build_stat_topic(s_root_topic, name, topic, sizeof(topic));
    JsonDocument doc;
    doc["Position"]  = position;
    doc["Direction"] = direction;
    doc["Target"]    = target;
    // iter 022 : additive hint, emitted only for non-Shutter so a pure-shutter
    // deployment keeps byte-identical stat JSON.
    if (device_type != 0) {
      doc["Type"] = device_profile::name(device_profile::from_u8(device_type));
    }
    char payload[96];
    const size_t n = serializeJson(doc, payload, sizeof(payload));
    return s_client.publish(topic, reinterpret_cast<const uint8_t*>(payload), n, false);
  }

  bool publish_shutter_duration(const char* name,
                                uint32_t open_time_ms, uint32_t close_time_ms) {
    if (!s_client.connected() || name == nullptr) return false;
    char topic[16 + nvs_store::MAX_TOPIC_LEN + nvs_store::MAX_NAME_LEN];
    build_stat_topic(s_root_topic, name, topic, sizeof(topic));
    JsonDocument doc;
    doc["OpenDuration"]  = open_time_ms  / 1000.0;
    doc["CloseDuration"] = close_time_ms / 1000.0;
    char payload[96];
    const size_t n = serializeJson(doc, payload, sizeof(payload));
    return s_client.publish(topic, reinterpret_cast<const uint8_t*>(payload), n, false);
  }

  bool publish_shutter_error(const char* name, const char* error) {
    if (!s_client.connected() || name == nullptr || error == nullptr) return false;
    char topic[16 + nvs_store::MAX_TOPIC_LEN + nvs_store::MAX_NAME_LEN];
    build_stat_topic(s_root_topic, name, topic, sizeof(topic));
    JsonDocument doc;
    doc["error"] = error;
    char payload[96];
    const size_t n = serializeJson(doc, payload, sizeof(payload));
    return s_client.publish(topic, reinterpret_cast<const uint8_t*>(payload), n, false);
  }

  bool publish_sensor_aggregated() {
    if (!s_client.connected()) return false;
    char topic[16 + nvs_store::MAX_TOPIC_LEN];
    build_sensor_topic(s_root_topic, topic, sizeof(topic));

    nvs_store::Remote buf[nvs_store::MAX_REMOTES];
    const size_t n = nvs_store::list_remotes(buf, nvs_store::MAX_REMOTES);

    JsonDocument doc;
    for (size_t i = 0; i < n; ++i) {
      const orchestrator::RuntimeState rt =
          orchestrator::get_runtime(buf[i].id);
      JsonObject o = doc[buf[i].name.c_str()].to<JsonObject>();
      o["Position"]  = rt.position;
      o["Direction"] = rt.direction;
      o["Target"]    = rt.target;
      // iter 022 : additive hint, non-Shutter only (byte-identical for shutters).
      if (buf[i].device_type != 0) {
        o["Type"] = device_profile::name(device_profile::from_u8(buf[i].device_type));
      }
    }
    char payload[512];
    const size_t plen = serializeJson(doc, payload, sizeof(payload));
    // retained=true so a late subscriber (Sowel plugin, HA, dashboard) gets
    // the current snapshot immediately on subscribe — without waiting for
    // the next motion or state change to trigger a fresh publish.
    return s_client.publish(topic, reinterpret_cast<const uint8_t*>(payload), plen, true);
  }

}
