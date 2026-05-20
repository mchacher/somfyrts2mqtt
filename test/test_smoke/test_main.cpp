/**
 * @file test_main.cpp
 * @brief Smoke test for the Unity native test infrastructure.
 *
 * Intentionally trivial. Real tests against pure-logic modules
 * (nvs_store, rolling code, MQTT topic parsing) come in later iters.
 */
#include <unity.h>

void setUp() {}
void tearDown() {}

void test_smoke(void) {
  TEST_ASSERT_TRUE(true);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_smoke);
  return UNITY_END();
}
