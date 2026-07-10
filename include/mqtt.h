/**
 * @file mqtt.h
 * @brief MQTT client for the bridge -- iter 014 Tasmota-style Shutter API.
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cctype>

#include "config.h"

/**
 * @namespace mqtt
 * @brief MQTT client wrapping PubSubClient with Tasmota's Shutter MQTT
 *        subset, keyed by remote name (vs. Tasmota's integer index).
 *
 * Topic structure (with `<root>` = `mqtt.topic` from NVS, default
 * `somfyrts2mqtt` ; multi-bridge setups must set distinct topics via
 * the web UI) :
 *
 * Subscribed (commands)
 *   cmnd/<root>/<name>/Open
 *   cmnd/<root>/<name>/Close
 *   cmnd/<root>/<name>/Stop
 *   cmnd/<root>/<name>/Position        0..100
 *   cmnd/<root>/<name>/OpenDuration    <seconds float>
 *   cmnd/<root>/<name>/CloseDuration   <seconds float>
 *   cmnd/<root>/<name>/SetPosition     0..100   (no RF)
 *   cmnd/<root>/<name>/Toggle                   (iter 022: Gate single-button cycle)
 *
 * Published (state + telemetry)
 *   tele/<root>/LWT       Online | Offline    (retained + LWT)
 *   tele/<root>/SENSOR    {"<name1>":{...},"<name2>":{...}}   (1 Hz during motion)
 *   stat/<root>/<name>    {"Position":N,"Direction":D,"Target":T}    (per cmd)
 *
 * Pure helpers (topic parsing, topic building) are inline so the native
 * test env can exercise them without PubSubClient.
 */
namespace mqtt {

  /// Commands the orchestrator understands (used internally + by web_ui).
  enum class Command : uint8_t {
    Invalid,
    Up,
    Down,
    Stop,
    Program,
    Toggle,  ///< iter 022: emit a Gate's single configured RTS button (see device_profile).
  };

  /// Maximum command payload length accepted. Anything bigger is rejected.
  static constexpr size_t MAX_CMD_PAYLOAD_LEN = 16;

  /// Maximum remote name length (matches nvs_store::MAX_NAME_LEN).
  static constexpr size_t MAX_NAME_LEN = 32;

  /// Maximum verb length on the wire. Largest verb is "CloseDuration" = 13.
  static constexpr size_t MAX_VERB_LEN = 16;

  /// Initial reconnect delay ; doubled on each failure, capped at RECONNECT_MAX_MS.
  static constexpr unsigned long RECONNECT_BASE_MS = 5000UL;

  /// Upper cap for the exponential reconnect backoff.
  static constexpr unsigned long RECONNECT_MAX_MS  = 60000UL;

  /// TCP socket timeout (seconds) for the underlying WiFiClient.
  static constexpr uint16_t SOCKET_TIMEOUT_S = 15;

  /// MQTT-level keep-alive (seconds). PINGREQ once per period.
  static constexpr uint16_t KEEPALIVE_S = 60;

  /// LWT payloads (Tasmota convention).
  static constexpr const char* LWT_ONLINE  = "Online";
  static constexpr const char* LWT_OFFLINE = "Offline";

  // === Lifecycle ========================================================

  /// Initialise the MQTT client. Reads broker config + topic root from NVS.
  void init();

  /// Drive the client. Call from the main `loop()`.
  void loop();

  /// @return true when connected to the broker.
  bool is_connected();

  /**
   * @brief Drop the current MQTT connection.
   *
   * The reconnect loop in `loop()` re-establishes a new session on the
   * next tick using whatever broker config + topic root is currently in
   * NVS. Used by the web UI after the user edits MQTT settings.
   */
  void disconnect();

  /// @return live root topic (NVS value when set, else default `somfyrts2mqtt`).
  const char* get_root_topic();

  // === Publish (called by orchestrator) =================================

  /**
   * @brief Publish the per-remote `stat/<root>/<name>` ack JSON.
   * @param device_type  iter 022: raw device type. When non-zero (non-Shutter),
   *        an additive `"Type":"<name>"` field is included; a Shutter (0) emits
   *        exactly the legacy `{Position,Direction,Target}` JSON.
   * @return false if not connected.
   */
  bool publish_shutter_state(const char* name,
                             uint8_t position, int8_t direction, uint8_t target,
                             uint8_t device_type = 0);

  /**
   * @brief Publish a per-remote `stat/<root>/<name>` ack after a duration update.
   */
  bool publish_shutter_duration(const char* name,
                                uint32_t open_time_ms, uint32_t close_time_ms);

  /**
   * @brief Publish a per-remote `stat/<root>/<name>` error JSON.
   * Used e.g. when Position is issued on an uncalibrated remote.
   */
  bool publish_shutter_error(const char* name, const char* error);

  /**
   * @brief Publish the aggregated `tele/<root>/SENSOR` payload.
   *
   * Iterates all known remotes (via `nvs_store::list_remotes`), reads
   * their runtime state from `orchestrator`, and assembles a single
   * JSON `{"<name>":{Position,Direction,Target}, ...}` payload.
   */
  bool publish_sensor_aggregated();

  // === Pure helpers (inline ; testable without PubSubClient) ============

  /// @return canonical lowercase string for @p cmd ("" for `Invalid`).
  inline const char* command_to_str(Command cmd) {
    switch (cmd) {
      case Command::Up:      return "up";
      case Command::Down:    return "down";
      case Command::Stop:    return "stop";
      case Command::Program: return "program";
      case Command::Toggle:  return "toggle";
      default:               return "";
    }
  }

  /// Decode a brief command payload (case-insensitive).
  inline Command parse_command(const char* str, size_t len) {
    if (str == nullptr || len == 0 || len > MAX_CMD_PAYLOAD_LEN) {
      return Command::Invalid;
    }
    char buf[MAX_CMD_PAYLOAD_LEN + 1];
    for (size_t i = 0; i < len; ++i) {
      buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(str[i])));
    }
    buf[len] = '\0';
    if (std::strcmp(buf, "up") == 0)      return Command::Up;
    if (std::strcmp(buf, "down") == 0)    return Command::Down;
    if (std::strcmp(buf, "stop") == 0)    return Command::Stop;
    if (std::strcmp(buf, "program") == 0) return Command::Program;
    return Command::Invalid;
  }

  /**
   * @brief Parse a `cmnd/<root>/<name>/<verb>` topic.
   * @param topic     NUL-terminated topic.
   * @param root      Expected root prefix (NVS-configured).
   * @param out_name  Buffer for the remote name (caller-provided).
   * @param name_cap  Capacity of @p out_name (incl. NUL).
   * @param out_verb  Buffer for the verb.
   * @param verb_cap  Capacity of @p out_verb (incl. NUL).
   * @return false on wrong prefix, wrong root, missing verb, empty name,
   *         or any segment too long for the provided buffers.
   */
  inline bool parse_cmnd_topic(const char* topic, const char* root,
                               char* out_name, size_t name_cap,
                               char* out_verb, size_t verb_cap) {
    if (topic == nullptr || root == nullptr ||
        out_name == nullptr || out_verb == nullptr ||
        name_cap == 0 || verb_cap == 0) return false;

    static constexpr const char* PREFIX = "cmnd/";
    static constexpr size_t      PREFIX_LEN = 5;
    if (std::strncmp(topic, PREFIX, PREFIX_LEN) != 0) return false;

    const size_t root_len = std::strlen(root);
    if (root_len == 0) return false;
    if (std::strncmp(topic + PREFIX_LEN, root, root_len) != 0) return false;
    if (topic[PREFIX_LEN + root_len] != '/') return false;

    // After "cmnd/<root>/" we want "<name>/<verb>".
    const char* name_begin = topic + PREFIX_LEN + root_len + 1;
    const char* slash = std::strchr(name_begin, '/');
    if (slash == nullptr) return false;

    const size_t name_len = static_cast<size_t>(slash - name_begin);
    if (name_len == 0 || name_len + 1 > name_cap) return false;
    std::memcpy(out_name, name_begin, name_len);
    out_name[name_len] = '\0';

    const char* verb_begin = slash + 1;
    if (*verb_begin == '\0') return false;
    // Reject a third slash (extra topic segment).
    if (std::strchr(verb_begin, '/') != nullptr) return false;
    const size_t verb_len = std::strlen(verb_begin);
    if (verb_len + 1 > verb_cap) return false;
    std::memcpy(out_verb, verb_begin, verb_len + 1);
    return true;
  }

  /// Write "cmnd/<root>/+/+" + NUL into @p out. Returns chars written (excluding NUL).
  inline size_t build_cmnd_subscription(const char* root, char* out, size_t cap) {
    return static_cast<size_t>(std::snprintf(out, cap, "cmnd/%s/+/+", root));
  }

  /// Write "tele/<root>/LWT" + NUL into @p out.
  inline size_t build_lwt_topic(const char* root, char* out, size_t cap) {
    return static_cast<size_t>(std::snprintf(out, cap, "tele/%s/LWT", root));
  }

  /// Write "tele/<root>/SENSOR" + NUL into @p out.
  inline size_t build_sensor_topic(const char* root, char* out, size_t cap) {
    return static_cast<size_t>(std::snprintf(out, cap, "tele/%s/SENSOR", root));
  }

  /// Write "stat/<root>/<name>" + NUL into @p out.
  inline size_t build_stat_topic(const char* root, const char* name,
                                 char* out, size_t cap) {
    return static_cast<size_t>(std::snprintf(out, cap, "stat/%s/%s", root, name));
  }

  /// Decode a PubSubClient state code (`-4..5`) to a short symbolic name.
  inline const char* state_str(int rc) {
    switch (rc) {
      case -4: return "CONNECTION_TIMEOUT";
      case -3: return "CONNECTION_LOST";
      case -2: return "CONNECT_FAILED";
      case -1: return "DISCONNECTED";
      case  0: return "CONNECTED";
      case  1: return "CONNECT_BAD_PROTOCOL";
      case  2: return "CONNECT_BAD_CLIENT_ID";
      case  3: return "CONNECT_UNAVAILABLE";
      case  4: return "CONNECT_BAD_CREDENTIALS";
      case  5: return "CONNECT_UNAUTHORIZED";
      default: return "?";
    }
  }

}
