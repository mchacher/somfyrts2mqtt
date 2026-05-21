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
   * @brief Send a Somfy RTS frame (short-command path).
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
   *
   * Goes through `Legion2/Somfy_Remote_Lib::sendCommandWithCode()`,
   * which inserts a 30 ms gap between frames. That is too wide for a
   * Somfy receiver to perceive a continuous press, so the long-press
   * variants (Pair 3 s, Erase 7 s) use `send_somfy_longpress()` below
   * instead.
   */
  bool send_somfy(uint32_t remote_id, uint16_t rolling_code, uint8_t button, int repeat = 1);

  /**
   * @brief Send a Somfy RTS frame with tight inter-frame timing (long-press path).
   * @param remote_id     24-bit transmitter id.
   * @param rolling_code  16-bit rolling code already persisted in NVS.
   * @param button        Somfy button byte (typically `0x08` PROG for the
   *                      Pair / Erase workflows).
   * @param repeat        Number of repeat frames after the initial one.
   *                      25 ≈ 3 s (Pair), 60 ≈ 7 s (Erase). Larger values
   *                      proportionally block the main loop.
   * @return true on success; false if the RF stack is not ready or button==0.
   *
   * Bypasses the Legion2 lib entirely: builds the obfuscated frame with
   * `rts_frame::build_frame()` and bit-bangs it on `CC1101_GDO0` with an
   * inter-frame gap of 3 ms (vs the lib's 30 ms). This is what gets a
   * Somfy receiver to perceive a real long press for entering pair /
   * erase modes from the web UI.
   */
  bool send_somfy_longpress(uint32_t remote_id, uint16_t rolling_code,
                            uint8_t button, int repeat);

}
