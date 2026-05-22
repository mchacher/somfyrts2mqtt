/**
 * @file test_main.cpp
 * @brief Native unit tests for the boot-counter state machine.
 *
 * The captive-portal trigger and the post-stable reset are pure logic in
 * include/wifi_boot.h. Hardware behavior (Preferences round-trip, NVS
 * write timing) is validated separately on real hardware via the boot
 * diagnostic log.
 */
#include <unity.h>
#include "wifi_boot.h"

using namespace wifi_boot;

void setUp() {}
void tearDown() {}

// === on_boot : increment and saturation ===

void test_on_boot_increments_from_zero(void) {
  TEST_ASSERT_EQUAL_UINT8(1, on_boot(0, 4));
}

void test_on_boot_increments_under_threshold(void) {
  TEST_ASSERT_EQUAL_UINT8(2, on_boot(1, 4));
  TEST_ASSERT_EQUAL_UINT8(3, on_boot(2, 4));
  TEST_ASSERT_EQUAL_UINT8(4, on_boot(3, 4));
}

void test_on_boot_saturates_at_threshold(void) {
  // Already-triggered counter stays at threshold rather than wrapping.
  // Guards against a missed NVS reset write between the trigger and the
  // AP branch.
  TEST_ASSERT_EQUAL_UINT8(4, on_boot(4, 4));
  TEST_ASSERT_EQUAL_UINT8(4, on_boot(5, 4));   // stale value from a crash
  TEST_ASSERT_EQUAL_UINT8(4, on_boot(255, 4)); // uint8 max
}

void test_on_boot_threshold_3_behaves(void) {
  // Threshold is a parameter; the math is the same for any value.
  TEST_ASSERT_EQUAL_UINT8(1, on_boot(0, 3));
  TEST_ASSERT_EQUAL_UINT8(3, on_boot(2, 3));
  TEST_ASSERT_EQUAL_UINT8(3, on_boot(3, 3));
}

// === should_force_ap : threshold check on the post-increment value ===

void test_should_force_ap_below_threshold(void) {
  TEST_ASSERT_FALSE(should_force_ap(0, 4));
  TEST_ASSERT_FALSE(should_force_ap(1, 4));
  TEST_ASSERT_FALSE(should_force_ap(3, 4));
}

void test_should_force_ap_at_threshold(void) {
  TEST_ASSERT_TRUE(should_force_ap(4, 4));
}

void test_should_force_ap_above_threshold(void) {
  // Stale counter (e.g. crash before reset) still triggers AP.
  TEST_ASSERT_TRUE(should_force_ap(5, 4));
  TEST_ASSERT_TRUE(should_force_ap(255, 4));
}

// === should_reset : non-zero AND uptime past stable_ms ===

void test_should_reset_zero_count_never_resets(void) {
  TEST_ASSERT_FALSE(should_reset(0, 0,      5000));
  TEST_ASSERT_FALSE(should_reset(0, 4999,   5000));
  TEST_ASSERT_FALSE(should_reset(0, 5000,   5000));
  TEST_ASSERT_FALSE(should_reset(0, 100000, 5000));
}

void test_should_reset_below_stable_uptime(void) {
  TEST_ASSERT_FALSE(should_reset(1, 0,    5000));
  TEST_ASSERT_FALSE(should_reset(1, 4999, 5000));
  TEST_ASSERT_FALSE(should_reset(3, 1000, 5000));
}

void test_should_reset_at_stable_uptime(void) {
  TEST_ASSERT_TRUE(should_reset(1, 5000, 5000));
  TEST_ASSERT_TRUE(should_reset(3, 5000, 5000));
}

void test_should_reset_well_past_stable(void) {
  TEST_ASSERT_TRUE(should_reset(2, 100000, 5000));
  // The counter can be at or above the AP threshold (we reset to 0 right
  // before starting AP, but a stale write could leave it set).
  TEST_ASSERT_TRUE(should_reset(4, 100000, 5000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_on_boot_increments_from_zero);
  RUN_TEST(test_on_boot_increments_under_threshold);
  RUN_TEST(test_on_boot_saturates_at_threshold);
  RUN_TEST(test_on_boot_threshold_3_behaves);
  RUN_TEST(test_should_force_ap_below_threshold);
  RUN_TEST(test_should_force_ap_at_threshold);
  RUN_TEST(test_should_force_ap_above_threshold);
  RUN_TEST(test_should_reset_zero_count_never_resets);
  RUN_TEST(test_should_reset_below_stable_uptime);
  RUN_TEST(test_should_reset_at_stable_uptime);
  RUN_TEST(test_should_reset_well_past_stable);
  return UNITY_END();
}
