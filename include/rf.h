/**
 * @file rf.h
 * @brief RF emission for Somfy RTS frames.
 *
 * Iter 004 ships a stub implementation that only logs. Iter 005/006
 * will swap the body for real CC1101 emission without changing this
 * header — the orchestrator and the rest of the firmware are kept
 * decoupled from the radio.
 */
#pragma once

#include <cstdint>

/**
 * @namespace rf
 * @brief Somfy RTS emission over the CC1101 transceiver.
 */
namespace rf {

  /**
   * @brief Initialise the RF stack.
   * @return true on success. The stub always returns true.
   */
  bool init();

  /**
   * @brief Send a Somfy RTS frame.
   * @param remote_id     24-bit transmitter id.
   * @param rolling_code  Counter to embed in the frame. Must have been
   *                      persisted to NVS before calling (see orchestrator).
   * @param button        Somfy button byte (`0x01` MY, `0x02` UP,
   *                      `0x04` DOWN, `0x08` PROG).
   * @param repeat        How many times to repeat the data frame after the
   *                      initial sync=2 frame. Default 1 (~360 ms total
   *                      blocking). Use 4 for PROG/pairing where extra
   *                      reliability matters (~800 ms total).
   * @return true on success; false if the RF stack is not ready.
   */
  bool send_somfy(uint32_t remote_id, uint16_t rolling_code, uint8_t button, int repeat = 1);

  /**
   * @brief Emit a Somfy RTS **80-bit "Toggle"** frame (iter 022).
   *
   * The single-button gate command is not a normal button byte: it is an
   * 80-bit frame with `frame[0] = 0xA4`, `frame[1]` high-nibble `0xF`
   * (RTWProto marker) and a 3-byte extension. The classic 56-bit path
   * (`send_somfy`, Legion2/Somfy_Remote_Lib, fixed `frame[0] = 0xA7`) cannot
   * produce it — this is a dedicated encoder + OOK transmitter, faithful to
   * the frame format used by rstrouse/ESPSomfy-RTS.
   *
   * @param remote_id     24-bit transmitter id.
   * @param rolling_code  Counter to embed. Persist to NVS before calling.
   * @param repeat        Extra frames after the initial (sync=12) frame.
   * @return true on success; false if the RF stack is not ready.
   */
  bool send_toggle(uint32_t remote_id, uint16_t rolling_code, int repeat = 4);

}
