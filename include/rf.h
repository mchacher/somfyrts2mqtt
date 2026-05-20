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
   * @param button        Somfy button bitmap (`0x01` MY, `0x02` UP,
   *                      `0x04` DOWN, `0x08` MY+UP, `0x80` PROG, ...).
   * @return true on success; false if the RF stack is not ready.
   */
  bool send_somfy(uint32_t remote_id, uint16_t rolling_code, uint8_t button);

}
