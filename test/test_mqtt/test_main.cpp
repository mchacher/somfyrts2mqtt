/**
 * @file test_main.cpp
 * @brief Native unit tests for the pure-logic helpers in mqtt.h.
 */
#include <unity.h>
#include <cstring>
#include "mqtt.h"

void setUp() {}
void tearDown() {}

// === command parsing ===

void test_parse_command_valid(void) {
  TEST_ASSERT_EQUAL(mqtt::Command::Up,      mqtt::parse_command("up", 2));
  TEST_ASSERT_EQUAL(mqtt::Command::Down,    mqtt::parse_command("DOWN", 4));
  TEST_ASSERT_EQUAL(mqtt::Command::Stop,    mqtt::parse_command("Stop", 4));
  TEST_ASSERT_EQUAL(mqtt::Command::Program, mqtt::parse_command("program", 7));
}

void test_parse_command_invalid(void) {
  TEST_ASSERT_EQUAL(mqtt::Command::Invalid, mqtt::parse_command("", 0));
  TEST_ASSERT_EQUAL(mqtt::Command::Invalid, mqtt::parse_command("upx", 3));
  TEST_ASSERT_EQUAL(mqtt::Command::Invalid, mqtt::parse_command("stopping", 8));
  TEST_ASSERT_EQUAL(mqtt::Command::Invalid, mqtt::parse_command(nullptr, 0));
  // Oversized payload (> MAX_CMD_PAYLOAD_LEN)
  TEST_ASSERT_EQUAL(mqtt::Command::Invalid, mqtt::parse_command("aaaaaaaaaaaaaaaaa", 17));
}

// === set topic parsing ===

void test_parse_set_topic_valid(void) {
  uint32_t id = 0;
  TEST_ASSERT_TRUE(mqtt::parse_set_topic("somfy2mqtt/A1B2C3/set", id));
  TEST_ASSERT_EQUAL_UINT32(0xA1B2C3u, id);
}

void test_parse_set_topic_lowercase_hex(void) {
  uint32_t id = 0;
  TEST_ASSERT_TRUE(mqtt::parse_set_topic("somfy2mqtt/a1b2c3/set", id));
  TEST_ASSERT_EQUAL_UINT32(0xA1B2C3u, id);
}

void test_parse_set_topic_wrong_prefix(void) {
  uint32_t id = 0;
  TEST_ASSERT_FALSE(mqtt::parse_set_topic("other/A1B2C3/set", id));
}

void test_parse_set_topic_wrong_suffix(void) {
  uint32_t id = 0;
  TEST_ASSERT_FALSE(mqtt::parse_set_topic("somfy2mqtt/A1B2C3/state", id));
}

void test_parse_set_topic_bad_hex(void) {
  uint32_t id = 0;
  TEST_ASSERT_FALSE(mqtt::parse_set_topic("somfy2mqtt/XYZABC/set", id));
}

void test_parse_set_topic_short_id(void) {
  uint32_t id = 0;
  TEST_ASSERT_FALSE(mqtt::parse_set_topic("somfy2mqtt/A1B/set", id));
}

void test_parse_set_topic_null(void) {
  uint32_t id = 0;
  TEST_ASSERT_FALSE(mqtt::parse_set_topic(nullptr, id));
}

// === topic building ===

void test_build_state_topic(void) {
  char buf[24];
  mqtt::build_state_topic(0xA1B2C3u, buf);
  TEST_ASSERT_EQUAL_STRING("somfy2mqtt/A1B2C3/state", buf);
}

void test_build_rolling_code_topic(void) {
  char buf[32];
  mqtt::build_rolling_code_topic(0xA1B2C3u, buf);
  TEST_ASSERT_EQUAL_STRING("somfy2mqtt/A1B2C3/rolling_code", buf);
}

// === command_to_str ===

void test_command_to_str(void) {
  TEST_ASSERT_EQUAL_STRING("up",      mqtt::command_to_str(mqtt::Command::Up));
  TEST_ASSERT_EQUAL_STRING("down",    mqtt::command_to_str(mqtt::Command::Down));
  TEST_ASSERT_EQUAL_STRING("stop",    mqtt::command_to_str(mqtt::Command::Stop));
  TEST_ASSERT_EQUAL_STRING("program", mqtt::command_to_str(mqtt::Command::Program));
  TEST_ASSERT_EQUAL_STRING("",        mqtt::command_to_str(mqtt::Command::Invalid));
}

// === PubSubClient state decoding ===

void test_state_str_known(void) {
  TEST_ASSERT_EQUAL_STRING("CONNECTION_TIMEOUT",      mqtt::state_str(-4));
  TEST_ASSERT_EQUAL_STRING("CONNECTION_LOST",         mqtt::state_str(-3));
  TEST_ASSERT_EQUAL_STRING("CONNECT_FAILED",          mqtt::state_str(-2));
  TEST_ASSERT_EQUAL_STRING("DISCONNECTED",            mqtt::state_str(-1));
  TEST_ASSERT_EQUAL_STRING("CONNECTED",               mqtt::state_str(0));
  TEST_ASSERT_EQUAL_STRING("CONNECT_BAD_PROTOCOL",    mqtt::state_str(1));
  TEST_ASSERT_EQUAL_STRING("CONNECT_BAD_CLIENT_ID",   mqtt::state_str(2));
  TEST_ASSERT_EQUAL_STRING("CONNECT_UNAVAILABLE",     mqtt::state_str(3));
  TEST_ASSERT_EQUAL_STRING("CONNECT_BAD_CREDENTIALS", mqtt::state_str(4));
  TEST_ASSERT_EQUAL_STRING("CONNECT_UNAUTHORIZED",    mqtt::state_str(5));
}

void test_state_str_unknown(void) {
  TEST_ASSERT_EQUAL_STRING("?", mqtt::state_str(-5));
  TEST_ASSERT_EQUAL_STRING("?", mqtt::state_str(6));
  TEST_ASSERT_EQUAL_STRING("?", mqtt::state_str(99));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_command_valid);
  RUN_TEST(test_parse_command_invalid);
  RUN_TEST(test_parse_set_topic_valid);
  RUN_TEST(test_parse_set_topic_lowercase_hex);
  RUN_TEST(test_parse_set_topic_wrong_prefix);
  RUN_TEST(test_parse_set_topic_wrong_suffix);
  RUN_TEST(test_parse_set_topic_bad_hex);
  RUN_TEST(test_parse_set_topic_short_id);
  RUN_TEST(test_parse_set_topic_null);
  RUN_TEST(test_build_state_topic);
  RUN_TEST(test_build_rolling_code_topic);
  RUN_TEST(test_command_to_str);
  RUN_TEST(test_state_str_known);
  RUN_TEST(test_state_str_unknown);
  return UNITY_END();
}
