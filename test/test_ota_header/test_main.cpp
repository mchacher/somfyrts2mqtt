/**
 * @file test_main.cpp
 * @brief Native tests for the OTA image-header chip guard.
 *
 * `include/ota_guard.h` parses the `chip_id` out of an ESP image header
 * prefix so the WebOTA path can reject a binary built for another chip
 * before writing it. Pure inline functions, exercised here without any
 * Arduino / Update dependency. Covers : valid C3 / S3 headers, wrong magic,
 * a buffer shorter than the header, the exact-length boundary, a null
 * buffer, and the chip_name mapping.
 */
#include <unity.h>
#include <cstdint>
#include <cstring>

#include "ota_guard.h"

void setUp() {}
void tearDown() {}

using namespace ota_guard;

/// Build a minimal esp_image_header prefix (16 bytes) with the given chip_id
/// (little-endian at offset 12) and magic byte.
static void make_header(uint8_t out[16], uint16_t chip_id, uint8_t magic) {
  std::memset(out, 0xAA, 16);  // fill with non-zero noise to catch offset bugs
  out[0] = magic;
  out[CHIP_ID_OFFSET] = static_cast<uint8_t>(chip_id & 0xFF);
  out[CHIP_ID_OFFSET + 1] = static_cast<uint8_t>((chip_id >> 8) & 0xFF);
}

// === header_chip_id ===

void test_valid_c3_header(void) {
  uint8_t h[16];
  make_header(h, CHIP_ID_ESP32C3, IMAGE_MAGIC);
  TEST_ASSERT_EQUAL_INT32(CHIP_ID_ESP32C3, header_chip_id(h, sizeof(h)));
}

void test_valid_s3_header(void) {
  uint8_t h[16];
  make_header(h, CHIP_ID_ESP32S3, IMAGE_MAGIC);
  TEST_ASSERT_EQUAL_INT32(CHIP_ID_ESP32S3, header_chip_id(h, sizeof(h)));
}

void test_bad_magic_returns_invalid(void) {
  uint8_t h[16];
  make_header(h, CHIP_ID_ESP32S3, 0x00);  // not an ESP image
  TEST_ASSERT_EQUAL_INT32(CHIP_ID_INVALID, header_chip_id(h, sizeof(h)));
}

void test_too_short_returns_invalid(void) {
  uint8_t h[16];
  make_header(h, CHIP_ID_ESP32C3, IMAGE_MAGIC);
  // 13 bytes : one short of reaching the chip_id high byte at offset 13.
  TEST_ASSERT_EQUAL_INT32(CHIP_ID_INVALID, header_chip_id(h, MIN_HEADER_LEN - 1));
}

void test_exact_min_length_ok(void) {
  uint8_t h[16];
  make_header(h, CHIP_ID_ESP32S3, IMAGE_MAGIC);
  // exactly offset 12 + 2 bytes
  TEST_ASSERT_EQUAL_INT32(CHIP_ID_ESP32S3, header_chip_id(h, MIN_HEADER_LEN));
}

void test_null_buffer_returns_invalid(void) {
  TEST_ASSERT_EQUAL_INT32(CHIP_ID_INVALID, header_chip_id(nullptr, 64));
}

// === chip_name ===

void test_chip_name_known(void) {
  TEST_ASSERT_EQUAL_STRING("ESP32-C3", chip_name(CHIP_ID_ESP32C3));
  TEST_ASSERT_EQUAL_STRING("ESP32-S3", chip_name(CHIP_ID_ESP32S3));
}

void test_chip_name_unknown(void) {
  TEST_ASSERT_EQUAL_STRING("another chip", chip_name(0x1234));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_valid_c3_header);
  RUN_TEST(test_valid_s3_header);
  RUN_TEST(test_bad_magic_returns_invalid);
  RUN_TEST(test_too_short_returns_invalid);
  RUN_TEST(test_exact_min_length_ok);
  RUN_TEST(test_null_buffer_returns_invalid);
  RUN_TEST(test_chip_name_known);
  RUN_TEST(test_chip_name_unknown);
  return UNITY_END();
}
