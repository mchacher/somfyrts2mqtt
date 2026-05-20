/**
 * @file test_main.cpp
 * @brief Native unit tests for the pure-logic helpers in nvs_store.h.
 *
 * Only the inline helpers in the header are exercised here. The
 * Preferences-backed CRUD wiring is validated on hardware via the
 * boot diagnostic log.
 */
#include <unity.h>
#include <string>
#include "nvs_store.h"

void setUp() {}
void tearDown() {}

// === format / parse hex ===

void test_format_parse_roundtrip(void) {
  const uint32_t ids[] = {1u, 0xABCDEFu, 0xFFFFFFu};
  for (uint32_t id : ids) {
    char hex[7];
    nvs_store::format_id_hex(id, hex);
    uint32_t parsed = 0;
    TEST_ASSERT_TRUE(nvs_store::parse_id_hex(hex, parsed));
    TEST_ASSERT_EQUAL_UINT32(id, parsed);
  }
}

void test_parse_invalid(void) {
  uint32_t out = 0;
  TEST_ASSERT_FALSE(nvs_store::parse_id_hex("G12345", out));
  TEST_ASSERT_FALSE(nvs_store::parse_id_hex("12345",  out));   // 5 chars
  TEST_ASSERT_FALSE(nvs_store::parse_id_hex("1234567", out));  // 7 chars
  TEST_ASSERT_FALSE(nvs_store::parse_id_hex("",        out));
  TEST_ASSERT_FALSE(nvs_store::parse_id_hex(nullptr,   out));
}

// === id / name validation ===

void test_is_valid_id(void) {
  TEST_ASSERT_FALSE(nvs_store::is_valid_id(0));
  TEST_ASSERT_TRUE (nvs_store::is_valid_id(1));
  TEST_ASSERT_TRUE (nvs_store::is_valid_id(0xFFFFFF));
  TEST_ASSERT_FALSE(nvs_store::is_valid_id(0x1000000));
}

void test_is_valid_name(void) {
  TEST_ASSERT_FALSE(nvs_store::is_valid_name(""));
  TEST_ASSERT_TRUE (nvs_store::is_valid_name("a"));
  TEST_ASSERT_TRUE (nvs_store::is_valid_name(std::string(32, 'x')));
  TEST_ASSERT_FALSE(nvs_store::is_valid_name(std::string(33, 'x')));
}

// === CSV index manipulation ===

void test_index_add_idempotent(void) {
  std::string idx;
  nvs_store::index_add(idx, "A1B2C3");
  nvs_store::index_add(idx, "A1B2C3");
  TEST_ASSERT_EQUAL_STRING("A1B2C3", idx.c_str());
}

void test_index_add_two(void) {
  std::string idx;
  nvs_store::index_add(idx, "A1B2C3");
  nvs_store::index_add(idx, "D4E5F6");
  TEST_ASSERT_EQUAL_STRING("A1B2C3,D4E5F6", idx.c_str());
}

void test_index_remove_only(void) {
  std::string idx = "A1B2C3";
  TEST_ASSERT_TRUE(nvs_store::index_remove(idx, "A1B2C3"));
  TEST_ASSERT_TRUE(idx.empty());
}

void test_index_remove_first(void) {
  std::string idx = "A1B2C3,D4E5F6";
  TEST_ASSERT_TRUE(nvs_store::index_remove(idx, "A1B2C3"));
  TEST_ASSERT_EQUAL_STRING("D4E5F6", idx.c_str());
}

void test_index_remove_middle(void) {
  std::string idx = "AAAAAA,BBBBBB,CCCCCC";
  TEST_ASSERT_TRUE(nvs_store::index_remove(idx, "BBBBBB"));
  TEST_ASSERT_EQUAL_STRING("AAAAAA,CCCCCC", idx.c_str());
}

void test_index_remove_last(void) {
  std::string idx = "A1B2C3,D4E5F6";
  TEST_ASSERT_TRUE(nvs_store::index_remove(idx, "D4E5F6"));
  TEST_ASSERT_EQUAL_STRING("A1B2C3", idx.c_str());
}

void test_index_remove_absent(void) {
  std::string idx = "A1B2C3";
  TEST_ASSERT_FALSE(nvs_store::index_remove(idx, "D4E5F6"));
  TEST_ASSERT_EQUAL_STRING("A1B2C3", idx.c_str());
}

void test_index_remove_empty(void) {
  std::string idx;
  TEST_ASSERT_FALSE(nvs_store::index_remove(idx, "A1B2C3"));
  TEST_ASSERT_TRUE(idx.empty());
}

void test_index_contains(void) {
  TEST_ASSERT_TRUE (nvs_store::index_contains("A1B2C3", "A1B2C3"));
  TEST_ASSERT_TRUE (nvs_store::index_contains("A1B2C3,D4E5F6", "D4E5F6"));
  TEST_ASSERT_TRUE (nvs_store::index_contains("AAAAAA,A1B2C3,BBBBBB", "A1B2C3"));
  TEST_ASSERT_FALSE(nvs_store::index_contains("A1B2C3", "FFFFFF"));
  TEST_ASSERT_FALSE(nvs_store::index_contains("", "A1B2C3"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_format_parse_roundtrip);
  RUN_TEST(test_parse_invalid);
  RUN_TEST(test_is_valid_id);
  RUN_TEST(test_is_valid_name);
  RUN_TEST(test_index_add_idempotent);
  RUN_TEST(test_index_add_two);
  RUN_TEST(test_index_remove_only);
  RUN_TEST(test_index_remove_first);
  RUN_TEST(test_index_remove_middle);
  RUN_TEST(test_index_remove_last);
  RUN_TEST(test_index_remove_absent);
  RUN_TEST(test_index_remove_empty);
  RUN_TEST(test_index_contains);
  return UNITY_END();
}
