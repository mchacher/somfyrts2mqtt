/**
 * @file rts_frame.h
 * @brief Pure-logic Somfy RTS frame builder (no Arduino deps).
 *
 * Extracted into its own header so that `test/test_rts_frame` can
 * include it from the native env, where Arduino.h is unavailable.
 *
 * Reference: Pushstack Somfy RTS reverse-engineering
 *   https://pushstack.wordpress.com/somfy-rts-protocol/
 *
 * Byte layout matches `Legion2/Somfy_Remote_Lib::buildFrame()` (used
 * by `rf::send_somfy()` for the short-command path) -- the native
 * test pins this with a byte-for-byte vector.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace rts_frame {

  /// Number of bytes in a Somfy RTS frame, on the wire.
  constexpr size_t SIZE = 7;

  /**
   * @brief Build an obfuscated Somfy RTS frame ready for emission.
   * @param button         Somfy button (`0x01` MY, `0x02` UP, `0x04` DOWN,
   *                       `0x08` PROG). Only the lower 4 bits are used.
   * @param rolling_code   16-bit rolling code, big-endian on the wire.
   * @param remote_id      24-bit remote id, big-endian on the wire (the
   *                       upper byte is ignored).
   * @param out            Output buffer of `SIZE` bytes, fully overwritten.
   *
   * The function is deterministic: same inputs always yield the same
   * 7 bytes. The output is the post-obfuscation frame -- caller pushes
   * it bit-by-bit on the RF line.
   *
   * Layout before obfuscation:
   *   out[0] = 0xA7                       // encryption key (constant)
   *   out[1] = (button << 4) | checksum   // upper nibble = button
   *   out[2] = code >> 8                  // big-endian rolling code
   *   out[3] = code & 0xFF
   *   out[4] = (id >> 16) & 0xFF
   *   out[5] = (id >>  8) & 0xFF
   *   out[6] =  id        & 0xFF
   *
   * Checksum is the XOR of all nibbles (4 LSB after masking).
   * Obfuscation: out[i] ^= out[i-1] for i in 1..6, in order.
   */
  inline void build_frame(uint8_t button,
                          uint16_t rolling_code,
                          uint32_t remote_id,
                          uint8_t out[SIZE]) {
    out[0] = 0xA7;
    out[1] = static_cast<uint8_t>((button & 0x0F) << 4);
    out[2] = static_cast<uint8_t>(rolling_code >> 8);
    out[3] = static_cast<uint8_t>(rolling_code & 0xFF);
    out[4] = static_cast<uint8_t>((remote_id >> 16) & 0xFF);
    out[5] = static_cast<uint8_t>((remote_id >>  8) & 0xFF);
    out[6] = static_cast<uint8_t>( remote_id        & 0xFF);

    // Checksum: XOR of every nibble. The `b ^ (b >> 4)` form below
    // collapses both nibbles of each byte into the same 4 bits, and
    // a final `& 0x0F` keeps only those. Mathematically equivalent
    // to iterating one nibble at a time.
    uint8_t cks = 0;
    for (size_t i = 0; i < SIZE; ++i) {
      cks = static_cast<uint8_t>(cks ^ out[i] ^ (out[i] >> 4));
    }
    cks &= 0x0F;
    out[1] = static_cast<uint8_t>(out[1] | cks);

    // Obfuscation: XOR cascade from out[1] onwards. Each out[i] is
    // XORed with the value of out[i-1] AFTER its own obfuscation
    // (out[0] stays untouched, so out[1] sees the original 0xA7).
    for (size_t i = 1; i < SIZE; ++i) {
      out[i] = static_cast<uint8_t>(out[i] ^ out[i - 1]);
    }
  }

}
