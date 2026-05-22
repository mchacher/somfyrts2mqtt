/**
 * @file test_main.cpp
 * @brief Native tests for the pure-logic helpers in mqtt.h (iter 014).
 *
 * Covers : command_to_str / parse_command (still used by orchestrator),
 * parse_cmnd_topic (Tasmota-style cmd topic parser), build_*_topic
 * builders, and state_str. The Preferences-backed connect / publish
 * paths are validated on hardware.
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
  TEST_ASSERT_EQUAL(mqtt::Command::Invalid, mqtt::parse_command("aaaaaaaaaaaaaaaaa", 17));
}

// === Tasmota cmnd topic parsing ===

void test_parse_cmnd_topic_open(void) {
  char name[33], verb[16];
  TEST_ASSERT_TRUE(mqtt::parse_cmnd_topic(
      "cmnd/somfyrts2mqtt-AB12CD/kitchen/Open",
      "somfyrts2mqtt-AB12CD",
      name, sizeof(name), verb, sizeof(verb)));
  TEST_ASSERT_EQUAL_STRING("kitchen", name);
  TEST_ASSERT_EQUAL_STRING("Open", verb);
}

void test_parse_cmnd_topic_position(void) {
  char name[33], verb[16];
  TEST_ASSERT_TRUE(mqtt::parse_cmnd_topic(
      "cmnd/home/shutters/Bedroom-1/Position",
      "home/shutters",
      name, sizeof(name), verb, sizeof(verb)));
  TEST_ASSERT_EQUAL_STRING("Bedroom-1", name);
  TEST_ASSERT_EQUAL_STRING("Position", verb);
}

void test_parse_cmnd_topic_open_duration(void) {
  char name[33], verb[16];
  TEST_ASSERT_TRUE(mqtt::parse_cmnd_topic(
      "cmnd/bridge1/kitchen_shutter/OpenDuration",
      "bridge1",
      name, sizeof(name), verb, sizeof(verb)));
  TEST_ASSERT_EQUAL_STRING("kitchen_shutter", name);
  TEST_ASSERT_EQUAL_STRING("OpenDuration", verb);
}

void test_parse_cmnd_topic_wrong_prefix(void) {
  char name[33], verb[16];
  TEST_ASSERT_FALSE(mqtt::parse_cmnd_topic(
      "tele/somfyrts2mqtt/kitchen/Open", "somfyrts2mqtt",
      name, sizeof(name), verb, sizeof(verb)));
  TEST_ASSERT_FALSE(mqtt::parse_cmnd_topic(
      "stat/somfyrts2mqtt/kitchen/Open", "somfyrts2mqtt",
      name, sizeof(name), verb, sizeof(verb)));
}

void test_parse_cmnd_topic_wrong_root(void) {
  char name[33], verb[16];
  TEST_ASSERT_FALSE(mqtt::parse_cmnd_topic(
      "cmnd/other-bridge/kitchen/Open", "somfyrts2mqtt",
      name, sizeof(name), verb, sizeof(verb)));
}

void test_parse_cmnd_topic_missing_verb(void) {
  char name[33], verb[16];
  TEST_ASSERT_FALSE(mqtt::parse_cmnd_topic(
      "cmnd/bridge/kitchen", "bridge",
      name, sizeof(name), verb, sizeof(verb)));
  TEST_ASSERT_FALSE(mqtt::parse_cmnd_topic(
      "cmnd/bridge/kitchen/", "bridge",
      name, sizeof(name), verb, sizeof(verb)));
}

void test_parse_cmnd_topic_empty_name(void) {
  char name[33], verb[16];
  TEST_ASSERT_FALSE(mqtt::parse_cmnd_topic(
      "cmnd/bridge//Open", "bridge",
      name, sizeof(name), verb, sizeof(verb)));
}

void test_parse_cmnd_topic_extra_segment(void) {
  char name[33], verb[16];
  TEST_ASSERT_FALSE(mqtt::parse_cmnd_topic(
      "cmnd/bridge/kitchen/Open/extra", "bridge",
      name, sizeof(name), verb, sizeof(verb)));
}

void test_parse_cmnd_topic_null_args(void) {
  char name[33], verb[16];
  TEST_ASSERT_FALSE(mqtt::parse_cmnd_topic(
      nullptr, "bridge", name, sizeof(name), verb, sizeof(verb)));
  TEST_ASSERT_FALSE(mqtt::parse_cmnd_topic(
      "cmnd/bridge/kitchen/Open", nullptr,
      name, sizeof(name), verb, sizeof(verb)));
}

// === topic building ===

void test_build_cmnd_subscription(void) {
  char buf[64];
  mqtt::build_cmnd_subscription("somfyrts2mqtt-AB12CD", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("cmnd/somfyrts2mqtt-AB12CD/+/+", buf);
}

void test_build_lwt_topic(void) {
  char buf[64];
  mqtt::build_lwt_topic("somfyrts2mqtt-AB12CD", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("tele/somfyrts2mqtt-AB12CD/LWT", buf);
}

void test_build_sensor_topic(void) {
  char buf[64];
  mqtt::build_sensor_topic("home/shutters/bridge1", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("tele/home/shutters/bridge1/SENSOR", buf);
}

void test_build_stat_topic(void) {
  char buf[96];
  mqtt::build_stat_topic("bridge1", "kitchen_shutter", buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("stat/bridge1/kitchen_shutter", buf);
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
  RUN_TEST(test_parse_cmnd_topic_open);
  RUN_TEST(test_parse_cmnd_topic_position);
  RUN_TEST(test_parse_cmnd_topic_open_duration);
  RUN_TEST(test_parse_cmnd_topic_wrong_prefix);
  RUN_TEST(test_parse_cmnd_topic_wrong_root);
  RUN_TEST(test_parse_cmnd_topic_missing_verb);
  RUN_TEST(test_parse_cmnd_topic_empty_name);
  RUN_TEST(test_parse_cmnd_topic_extra_segment);
  RUN_TEST(test_parse_cmnd_topic_null_args);
  RUN_TEST(test_build_cmnd_subscription);
  RUN_TEST(test_build_lwt_topic);
  RUN_TEST(test_build_sensor_topic);
  RUN_TEST(test_build_stat_topic);
  RUN_TEST(test_command_to_str);
  RUN_TEST(test_state_str_known);
  RUN_TEST(test_state_str_unknown);
  return UNITY_END();
}
