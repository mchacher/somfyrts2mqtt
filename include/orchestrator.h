/**
 * @file orchestrator.h
 * @brief Wires incoming MQTT commands to NVS persistence and RF emission.
 */
#pragma once

#include <cstdint>
#include "mqtt.h"

/**
 * @namespace orchestrator
 * @brief End-to-end command dispatch.
 *
 * On every incoming `somfy2mqtt/<id>/set`, looks the remote up in NVS,
 * increments and persists its rolling code, calls `rf::send_somfy`,
 * and publishes the new state and rolling_code as retained MQTT
 * messages. Persist-before-emit guarantees no rolling-code replay
 * after an unexpected reboot.
 */
namespace orchestrator {

  /**
   * @brief Handle an incoming MQTT command for a given remote.
   * @param remote_id  24-bit id parsed from the topic.
   * @param cmd        Decoded command (never `Invalid` — mqtt drops those upstream).
   *
   * Drops silently (with a warn log) on unknown remote id or NVS error.
   */
  void handle_command(uint32_t remote_id, mqtt::Command cmd);

  /**
   * @brief Map an MQTT command to its Somfy button byte.
   * @return Somfy button value (`0x02` Up, `0x04` Down, `0x01` Stop/My,
   *         `0x08` Program), or `0x00` for `Invalid`.
   *
   * Values match `enum class Command` in Legion2/Somfy_Remote_Lib. The
   * Somfy RTS frame stores the button in the upper nibble of frame[1]
   * (see `SomfyRemote::buildFrame`), so it has to be one of the 4-bit
   * codes 0x1..0xA, not 0x80.
   */
  inline uint8_t command_to_button(mqtt::Command cmd) {
    switch (cmd) {
      case mqtt::Command::Up:      return 0x02;
      case mqtt::Command::Down:    return 0x04;
      case mqtt::Command::Stop:    return 0x01;
      case mqtt::Command::Program: return 0x08;
      default:                     return 0x00;
    }
  }

  /**
   * @brief Parse a command name (case-insensitive) into an `mqtt::Command`.
   * @param s  Null-terminated string. Accepts "up", "down", "stop", "program".
   * @return `mqtt::Command::Invalid` on null, unknown name, or oversized input.
   *
   * Used by the web UI to map a button payload to the canonical command path.
   */
  inline mqtt::Command command_from_str(const char* s) {
    if (s == nullptr) return mqtt::Command::Invalid;
    char buf[8] = {0};
    size_t i = 0;
    for (; s[i] != '\0' && i < 7; ++i) {
      const char c = s[i];
      buf[i] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    if (s[i] != '\0') return mqtt::Command::Invalid;  // longer than 7 chars
    buf[i] = '\0';
    if (i == 2 && buf[0] == 'u' && buf[1] == 'p')                                   return mqtt::Command::Up;
    if (i == 4 && buf[0] == 'd' && buf[1] == 'o' && buf[2] == 'w' && buf[3] == 'n') return mqtt::Command::Down;
    if (i == 4 && buf[0] == 's' && buf[1] == 't' && buf[2] == 'o' && buf[3] == 'p') return mqtt::Command::Stop;
    if (i == 7 && buf[0] == 'p' && buf[1] == 'r' && buf[2] == 'o' &&
        buf[3] == 'g' && buf[4] == 'r' && buf[5] == 'a' && buf[6] == 'm')           return mqtt::Command::Program;
    return mqtt::Command::Invalid;
  }

}
