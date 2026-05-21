/**
 * @file test_main.cpp
 * @brief Native unit tests for the pure-logic helpers in orchestrator.h.
 *
 * Only `command_to_button` is testable natively. The end-to-end
 * `handle_command` involves NVS / MQTT / RF and is validated on HW.
 */
#include <unity.h>
#include "orchestrator.h"

void setUp() {}
void tearDown() {}

void test_command_to_button_up(void) {
  TEST_ASSERT_EQUAL_UINT8(0x02, orchestrator::command_to_button(mqtt::Command::Up));
}

void test_command_to_button_down(void) {
  TEST_ASSERT_EQUAL_UINT8(0x04, orchestrator::command_to_button(mqtt::Command::Down));
}

void test_command_to_button_stop(void) {
  TEST_ASSERT_EQUAL_UINT8(0x01, orchestrator::command_to_button(mqtt::Command::Stop));
}

void test_command_to_button_program(void) {
  // 0x08, not 0x80: the Somfy button is a 4-bit value packed into the upper
  // nibble of frame[1] by SomfyRemote::buildFrame. 0x80 was a bug.
  TEST_ASSERT_EQUAL_UINT8(0x08, orchestrator::command_to_button(mqtt::Command::Program));
}

void test_command_to_button_invalid(void) {
  TEST_ASSERT_EQUAL_UINT8(0x00, orchestrator::command_to_button(mqtt::Command::Invalid));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_command_to_button_up);
  RUN_TEST(test_command_to_button_down);
  RUN_TEST(test_command_to_button_stop);
  RUN_TEST(test_command_to_button_program);
  RUN_TEST(test_command_to_button_invalid);
  return UNITY_END();
}
