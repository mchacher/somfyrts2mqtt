/**
 * @file test_main.cpp
 * @brief Native tests for the time-based shutter position estimator.
 *
 * The estimator lives in `include/shutter_state.h` as pure inline
 * functions, so we exercise it directly here without any Arduino
 * dependency. Covers : linear interpolation, clamping at the legal
 * boundaries, idle / uncalibrated short-circuits, the stop decision
 * for intermediate vs extreme targets, and the duration picker
 * with fallback when the close side is uncalibrated.
 */
#include <unity.h>
#include <cstdint>

#include "shutter_state.h"

void setUp() {}
void tearDown() {}

using namespace shutter_state;

// === estimate ===

void test_estimate_idle_returns_start(void) {
  TEST_ASSERT_EQUAL_UINT8(50, estimate(1000, 2000, 50, DIR_IDLE, 20000));
}

void test_estimate_uncalibrated_returns_start(void) {
  TEST_ASSERT_EQUAL_UINT8(50, estimate(1000, 2000, 50, DIR_OPENING, 0));
}

void test_estimate_opening_quarter(void) {
  // start=0, duration=20s, elapsed=5s -> +25 -> 25.
  TEST_ASSERT_EQUAL_UINT8(25, estimate(1000, 6000, 0, DIR_OPENING, 20000));
}

void test_estimate_opening_half(void) {
  TEST_ASSERT_EQUAL_UINT8(50, estimate(1000, 11000, 0, DIR_OPENING, 20000));
}

void test_estimate_closing_half(void) {
  // start=100, duration=20s, elapsed=10s -> -50 -> 50.
  TEST_ASSERT_EQUAL_UINT8(50, estimate(0, 10000, 100, DIR_CLOSING, 20000));
}

void test_estimate_clamp_open_max(void) {
  // start=80, opening, elapsed exceeds remaining duration.
  TEST_ASSERT_EQUAL_UINT8(100, estimate(0, 30000, 80, DIR_OPENING, 20000));
}

void test_estimate_clamp_close_min(void) {
  // start=20, closing, elapsed exceeds remaining duration.
  TEST_ASSERT_EQUAL_UINT8(0, estimate(0, 30000, 20, DIR_CLOSING, 20000));
}

void test_estimate_overflow_safe(void) {
  // Very long elapsed (~ uint32 max) must not wrap.
  TEST_ASSERT_EQUAL_UINT8(100, estimate(0, 4000000000u, 0, DIR_OPENING, 20000));
}

void test_estimate_now_before_start_treated_as_zero_elapsed(void) {
  // Defensive : clock skew should not produce a wrap-around delta.
  TEST_ASSERT_EQUAL_UINT8(50, estimate(10000, 9000, 50, DIR_OPENING, 20000));
}

// === should_stop ===

void test_should_stop_intermediate_target_reached(void) {
  TEST_ASSERT_TRUE(should_stop(60, 60, DIR_OPENING));
}

void test_should_stop_intermediate_overshoot(void) {
  TEST_ASSERT_TRUE(should_stop(65, 60, DIR_OPENING));
}

void test_should_stop_intermediate_undershoot(void) {
  TEST_ASSERT_FALSE(should_stop(55, 60, DIR_OPENING));
}

void test_should_stop_intermediate_closing(void) {
  TEST_ASSERT_TRUE(should_stop(40, 40, DIR_CLOSING));
  TEST_ASSERT_TRUE(should_stop(35, 40, DIR_CLOSING));
  TEST_ASSERT_FALSE(should_stop(45, 40, DIR_CLOSING));
}

void test_should_stop_full_open_lets_motor_self_stop(void) {
  // target=100 -> caller waits for the motor's end-of-travel switch.
  TEST_ASSERT_FALSE(should_stop(100, 100, DIR_OPENING));
}

void test_should_stop_full_close_lets_motor_self_stop(void) {
  TEST_ASSERT_FALSE(should_stop(0, 0, DIR_CLOSING));
}

void test_should_stop_idle(void) {
  TEST_ASSERT_FALSE(should_stop(50, 50, DIR_IDLE));
}

// === duration_for ===

void test_duration_for_opening_uses_open_time(void) {
  TEST_ASSERT_EQUAL_UINT32(18000u, duration_for(DIR_OPENING, 18000, 20000));
}

void test_duration_for_closing_uses_close_time(void) {
  TEST_ASSERT_EQUAL_UINT32(20000u, duration_for(DIR_CLOSING, 18000, 20000));
}

void test_duration_for_closing_fallback_when_close_unset(void) {
  // Close time 0 falls back to open time.
  TEST_ASSERT_EQUAL_UINT32(18000u, duration_for(DIR_CLOSING, 18000, 0));
}

void test_duration_for_idle_is_zero(void) {
  TEST_ASSERT_EQUAL_UINT32(0u, duration_for(DIR_IDLE, 18000, 20000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_estimate_idle_returns_start);
  RUN_TEST(test_estimate_uncalibrated_returns_start);
  RUN_TEST(test_estimate_opening_quarter);
  RUN_TEST(test_estimate_opening_half);
  RUN_TEST(test_estimate_closing_half);
  RUN_TEST(test_estimate_clamp_open_max);
  RUN_TEST(test_estimate_clamp_close_min);
  RUN_TEST(test_estimate_overflow_safe);
  RUN_TEST(test_estimate_now_before_start_treated_as_zero_elapsed);
  RUN_TEST(test_should_stop_intermediate_target_reached);
  RUN_TEST(test_should_stop_intermediate_overshoot);
  RUN_TEST(test_should_stop_intermediate_undershoot);
  RUN_TEST(test_should_stop_intermediate_closing);
  RUN_TEST(test_should_stop_full_open_lets_motor_self_stop);
  RUN_TEST(test_should_stop_full_close_lets_motor_self_stop);
  RUN_TEST(test_should_stop_idle);
  RUN_TEST(test_duration_for_opening_uses_open_time);
  RUN_TEST(test_duration_for_closing_uses_close_time);
  RUN_TEST(test_duration_for_closing_fallback_when_close_unset);
  RUN_TEST(test_duration_for_idle_is_zero);
  return UNITY_END();
}
