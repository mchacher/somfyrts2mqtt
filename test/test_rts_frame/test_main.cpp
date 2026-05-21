/**
 * @file test_main.cpp
 * @brief Native tests for the Somfy RTS frame builder (iter 012).
 *
 * The frame builder lives in `include/rts_frame.h` as a pure-logic
 * inline function (no Arduino deps), so we can exercise it directly
 * in the native env. The reference vector below was derived by hand
 * from `Legion2/Somfy_Remote_Lib::buildFrame()` for a known input ;
 * a divergence here means our long-press path is no longer byte-
 * compatible with the lib path, which would be a regression.
 */
#include <unity.h>
#include <cstdint>
#include <cstring>

#include "rts_frame.h"

void setUp() {}
void tearDown() {}

// === Reference vector =====================================================
//
// Input :
//   button       = 0x02 (Up)
//   rolling_code = 0x1234
//   remote_id    = 0xA1B2C3
//
// Hand-derivation (mirrors `SomfyRemote::buildFrame()`) :
//   pre-checksum : A7 20 12 34 A1 B2 C3
//   checksum (XOR of every nibble & 0xF) = 0x6
//   post-checksum : A7 26 12 34 A1 B2 C3
//   obfuscation (out[i] ^= out[i-1], i in 1..6) :
//     out[1] = 0x26 ^ 0xA7 = 0x81
//     out[2] = 0x12 ^ 0x81 = 0x93
//     out[3] = 0x34 ^ 0x93 = 0xA7
//     out[4] = 0xA1 ^ 0xA7 = 0x06
//     out[5] = 0xB2 ^ 0x06 = 0xB4
//     out[6] = 0xC3 ^ 0xB4 = 0x77
static constexpr uint8_t REF_VECTOR[rts_frame::SIZE] = {
  0xA7, 0x81, 0x93, 0xA7, 0x06, 0xB4, 0x77
};

void test_build_frame_known_vector(void) {
  uint8_t out[rts_frame::SIZE] = {0};
  rts_frame::build_frame(/*button*/ 0x02,
                         /*code*/ 0x1234,
                         /*id*/ 0xA1B2C3,
                         out);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(REF_VECTOR, out, rts_frame::SIZE);
}

// De-obfuscate the frame in place (inverse of the XOR cascade) so we can
// inspect the underlying button / code / id bytes. Iterates from the end
// so each step uses the original (still-obfuscated) predecessor.
static void deobfuscate(uint8_t f[rts_frame::SIZE]) {
  for (int i = static_cast<int>(rts_frame::SIZE) - 1; i >= 1; --i) {
    f[i] = static_cast<uint8_t>(f[i] ^ f[i - 1]);
  }
}

void test_build_frame_encryption_key(void) {
  uint8_t out[rts_frame::SIZE] = {0};
  rts_frame::build_frame(0x02, 0x1234, 0xA1B2C3, out);
  // out[0] is never XORed -- always exactly the Somfy key 0xA7.
  TEST_ASSERT_EQUAL_HEX8(0xA7, out[0]);
}

void test_build_frame_button_in_upper_nibble_of_frame1(void) {
  uint8_t out[rts_frame::SIZE] = {0};
  rts_frame::build_frame(/*Prog*/ 0x08, 0x0000, 0x000000, out);
  deobfuscate(out);
  // The lower 4 bits are the checksum slot; we just check the upper nibble.
  TEST_ASSERT_EQUAL_UINT8(0x80, static_cast<uint8_t>(out[1] & 0xF0));
}

void test_build_frame_rolling_code_big_endian(void) {
  uint8_t out[rts_frame::SIZE] = {0};
  rts_frame::build_frame(0x02, /*code*/ 0xBEEF, 0x000000, out);
  deobfuscate(out);
  TEST_ASSERT_EQUAL_HEX8(0xBE, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0xEF, out[3]);
}

void test_build_frame_remote_id_big_endian(void) {
  uint8_t out[rts_frame::SIZE] = {0};
  rts_frame::build_frame(0x02, 0x0000, /*id*/ 0xA1B2C3, out);
  deobfuscate(out);
  TEST_ASSERT_EQUAL_HEX8(0xA1, out[4]);
  TEST_ASSERT_EQUAL_HEX8(0xB2, out[5]);
  TEST_ASSERT_EQUAL_HEX8(0xC3, out[6]);
}

void test_build_frame_program_button_lower_nibble(void) {
  // Regression vs the historical 0x80 bug : Program is 0x08 (4-bit), not
  // 0x80. Passing 0x80 used to land 0x00 in the upper nibble (since the
  // builder masks to 0x0F) and Somfy would ignore the frame. Verify that
  // 0x08 produces 0x80 in the upper nibble of frame[1].
  uint8_t out[rts_frame::SIZE] = {0};
  rts_frame::build_frame(/*Prog*/ 0x08, 0x0000, 0x000000, out);
  deobfuscate(out);
  TEST_ASSERT_EQUAL_UINT8(0x80, static_cast<uint8_t>(out[1] & 0xF0));
}

void test_build_frame_checksum_is_xor_of_nibbles(void) {
  // The lower 4 bits of frame[1] (after build) must be the XOR of all
  // 14 nibbles of the *pre-checksum* layout.
  // Manually compute for {0xA7, 0x20, 0x12, 0x34, 0xA1, 0xB2, 0xC3} :
  //   A^7=D ; 2^0=2 ; 1^2=3 ; 3^4=7 ; A^1=B ; B^2=9 ; C^3=F
  //   D^2=F ; F^3=C ; C^7=B ; B^B=0 ; 0^9=9 ; 9^F=6
  // -> checksum 0x6.
  uint8_t out[rts_frame::SIZE] = {0};
  rts_frame::build_frame(0x02, 0x1234, 0xA1B2C3, out);
  deobfuscate(out);
  TEST_ASSERT_EQUAL_UINT8(0x06, static_cast<uint8_t>(out[1] & 0x0F));
}

void test_build_frame_high_byte_of_id_is_dropped(void) {
  // 24-bit id : the top 8 bits of the uint32_t parameter must not leak.
  uint8_t out_truncated[rts_frame::SIZE] = {0};
  uint8_t out_with_top[rts_frame::SIZE]  = {0};
  rts_frame::build_frame(0x02, 0x1234, 0x00A1B2C3, out_truncated);
  rts_frame::build_frame(0x02, 0x1234, 0xFFA1B2C3, out_with_top);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(out_truncated, out_with_top, rts_frame::SIZE);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_build_frame_known_vector);
  RUN_TEST(test_build_frame_encryption_key);
  RUN_TEST(test_build_frame_button_in_upper_nibble_of_frame1);
  RUN_TEST(test_build_frame_rolling_code_big_endian);
  RUN_TEST(test_build_frame_remote_id_big_endian);
  RUN_TEST(test_build_frame_program_button_lower_nibble);
  RUN_TEST(test_build_frame_checksum_is_xor_of_nibbles);
  RUN_TEST(test_build_frame_high_byte_of_id_is_dropped);
  return UNITY_END();
}
