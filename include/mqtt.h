/**
 * @file mqtt.h
 * @brief MQTT client for the Somfy <-> MQTT bridge.
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
 * @brief MQTT client for the bridge.
 *
 * Wraps `PubSubClient`. Manages the bridge presence (retained "online" on
 * connect, LWT "offline" otherwise) and dispatches incoming
 * `somfy2mqtt/<remote_id>/set` messages to a registered handler.
 *
 * Pure helpers (topic / payload parsing, topic building) are inline so
 * native unit tests can exercise them without linking PubSubClient.
 */
namespace mqtt {

  /// Commands carried by `somfy2mqtt/<id>/set` payloads.
  enum class Command : uint8_t {
    Invalid,
    Up,
    Down,
    Stop,
    Program,
  };

  /**
   * @brief Handler signature for incoming commands.
   * @param remote_id  24-bit remote id parsed from the topic.
   * @param cmd        Decoded command (never `Invalid`).
   */
  using CommandHandler = void (*)(uint32_t remote_id, Command cmd);

  /// Maximum command payload length accepted. Anything bigger is rejected.
  static constexpr size_t MAX_CMD_PAYLOAD_LEN = 16;

  /// Reconnect attempt period (ms).
  static constexpr unsigned long RECONNECT_INTERVAL_MS = 5000UL;

  /// Bridge presence topic (retained, drives LWT).
  static constexpr const char* BRIDGE_STATE_TOPIC = MQTT_TOPIC_PREFIX "/bridge/state";
  static constexpr const char* BRIDGE_STATE_ONLINE  = "online";
  static constexpr const char* BRIDGE_STATE_OFFLINE = "offline";

  // === Lifecycle (implemented in src/mqtt.cpp) ===

  /**
   * @brief Initialise the MQTT client. Reads the broker config from NVS.
   * @param handler  Called on every well-formed incoming command. May be null.
   */
  void init(CommandHandler handler);

  /// Drive the client. Call from the main `loop()`.
  void loop();

  /// @return whether the client is currently connected to the broker.
  bool is_connected();

  /**
   * @brief Publish a retained state message for a remote.
   * @return false if not connected or the build buffer overflows (won't happen
   *         with current limits).
   */
  bool publish_state(uint32_t remote_id, Command last_cmd);

  /**
   * @brief Publish a retained rolling_code message for a remote.
   * @return false if not connected.
   */
  bool publish_rolling_code(uint32_t remote_id, uint16_t code);

  // === Pure helpers (inline; testable without PubSubClient) ===

  /// @return canonical lowercase string for @p cmd ("" for `Invalid`).
  inline const char* command_to_str(Command cmd) {
    switch (cmd) {
      case Command::Up:      return "up";
      case Command::Down:    return "down";
      case Command::Stop:    return "stop";
      case Command::Program: return "program";
      default:               return "";
    }
  }

  /**
   * @brief Decode a command payload (case-insensitive).
   * @param str  Payload bytes (not necessarily NUL-terminated).
   * @param len  Number of bytes.
   * @return `Command::Invalid` on null, oversized, unknown, or empty input.
   */
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
   * @brief Parse a `somfy2mqtt/<HEXID>/set` topic.
   * @param topic     NUL-terminated topic string.
   * @param remote_id Filled on success.
   * @return false on wrong prefix, wrong suffix, or non-hex id.
   */
  inline bool parse_set_topic(const char* topic, uint32_t& remote_id) {
    if (topic == nullptr) return false;
    static constexpr const char* PREFIX = MQTT_TOPIC_PREFIX "/";
    static constexpr size_t PREFIX_LEN  = sizeof(MQTT_TOPIC_PREFIX "/") - 1;
    static constexpr const char* SUFFIX = "/set";
    static constexpr size_t SUFFIX_LEN  = 4;
    static constexpr size_t ID_LEN      = 6;

    const size_t total_len = std::strlen(topic);
    if (total_len != PREFIX_LEN + ID_LEN + SUFFIX_LEN) return false;
    if (std::strncmp(topic, PREFIX, PREFIX_LEN) != 0) return false;
    if (std::strncmp(topic + PREFIX_LEN + ID_LEN, SUFFIX, SUFFIX_LEN) != 0) return false;

    uint32_t v = 0;
    for (size_t i = 0; i < ID_LEN; ++i) {
      const char c = topic[PREFIX_LEN + i];
      uint8_t nibble;
      if      (c >= '0' && c <= '9') nibble = static_cast<uint8_t>(c - '0');
      else if (c >= 'A' && c <= 'F') nibble = static_cast<uint8_t>(10 + c - 'A');
      else if (c >= 'a' && c <= 'f') nibble = static_cast<uint8_t>(10 + c - 'a');
      else return false;
      v = (v << 4) | nibble;
    }
    remote_id = v;
    return true;
  }

  /// Write "somfy2mqtt/<HEXID>/state" + NUL into @p out (24 bytes is enough).
  inline void build_state_topic(uint32_t remote_id, char out[24]) {
    std::snprintf(out, 24, MQTT_TOPIC_PREFIX "/%06X/state",
                  static_cast<unsigned>(remote_id & 0xFFFFFFu));
  }

  /// Write "somfy2mqtt/<HEXID>/rolling_code" + NUL into @p out (32 bytes is enough).
  inline void build_rolling_code_topic(uint32_t remote_id, char out[32]) {
    std::snprintf(out, 32, MQTT_TOPIC_PREFIX "/%06X/rolling_code",
                  static_cast<unsigned>(remote_id & 0xFFFFFFu));
  }

}
