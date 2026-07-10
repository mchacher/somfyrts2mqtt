/**
 * @file test_main.cpp
 * @brief Native tests for the per-remote device profile.
 *
 * `include/device_profile.h` is a pure header, exercised here without any
 * Arduino / NVS dependency. Covers: the position-tracking predicate, the
 * wire-label mapping, and the defensive `from_u8` decode (unknown value
 * degrades to Shutter, which is also the NVS default).
 */
#include <unity.h>
#include <cstdint>

#include "device_profile.h"

void setUp() {}
void tearDown() {}

using namespace device_profile;

void test_shutter_uses_position(void) {
  TEST_ASSERT_TRUE(uses_position(DeviceType::Shutter));
}

void test_gate_is_not_positional(void) {
  TEST_ASSERT_FALSE(uses_position(DeviceType::Gate));
}

void test_name_mapping(void) {
  TEST_ASSERT_EQUAL_STRING("shutter", name(DeviceType::Shutter));
  TEST_ASSERT_EQUAL_STRING("gate", name(DeviceType::Gate));
}

void test_from_u8_known(void) {
  TEST_ASSERT_TRUE(from_u8(0) == DeviceType::Shutter);
  TEST_ASSERT_TRUE(from_u8(1) == DeviceType::Gate);
}

void test_from_u8_unknown_degrades_to_shutter(void) {
  // Forward-written / corrupt value must not crash or mis-decode.
  TEST_ASSERT_TRUE(from_u8(2) == DeviceType::Shutter);
  TEST_ASSERT_TRUE(from_u8(255) == DeviceType::Shutter);
}

void test_shutter_is_zero(void) {
  // Contract with NVS: default (missing key) == 0 == Shutter.
  TEST_ASSERT_EQUAL_UINT8(0, static_cast<uint8_t>(DeviceType::Shutter));
}

void test_somfy_toggle_code(void) {
  // The Gate toggle emits the dedicated Somfy RTS Toggle command 0x0C.
  TEST_ASSERT_EQUAL_UINT8(0x0C, SOMFY_TOGGLE);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_shutter_uses_position);
  RUN_TEST(test_gate_is_not_positional);
  RUN_TEST(test_name_mapping);
  RUN_TEST(test_from_u8_known);
  RUN_TEST(test_from_u8_unknown_degrades_to_shutter);
  RUN_TEST(test_shutter_is_zero);
  RUN_TEST(test_somfy_toggle_code);
  return UNITY_END();
}
