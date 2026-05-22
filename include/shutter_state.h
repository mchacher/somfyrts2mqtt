/**
 * @file shutter_state.h
 * @brief Pure-logic position estimator for time-based shutter tracking.
 *
 * Somfy RTS is unidirectional : the motor never reports its state. The
 * only way to expose a position is to chronometer the motion ourselves.
 * After the user calibrates `OpenDuration` and `CloseDuration` (one
 * time, per remote), `estimate()` returns the live position at any
 * time during motion.
 *
 * Drift is real : motor wear, load variations, and external operation
 * (physical remote, wall switch) all desynchronise our state. The
 * orchestrator mitigates by snapping to 100 / 0 at the end of every
 * full Open / Close, which acts as a periodic recalibration.
 *
 * Pure header so that `test/test_shutter_state/` can exercise it from
 * the native env (no Arduino, no NVS, no MQTT).
 */
#pragma once

#include <cstdint>

namespace shutter_state {

  /// Position is 0 (fully closed) to 100 (fully open).
  constexpr uint8_t POSITION_MIN = 0;
  constexpr uint8_t POSITION_MAX = 100;

  /// Direction codes mirror Tasmota's Shutter convention.
  constexpr int8_t DIR_CLOSING = -1;
  constexpr int8_t DIR_IDLE    =  0;
  constexpr int8_t DIR_OPENING =  1;

  /**
   * @brief Estimate the current position given elapsed motion time.
   * @param motion_started_ms `millis()` at the moment the RF Up / Down
   *                          was emitted.
   * @param now_ms            current `millis()`.
   * @param start_position    position at `motion_started_ms`.
   * @param direction         `DIR_OPENING` or `DIR_CLOSING`. `DIR_IDLE`
   *                          returns `start_position` unchanged.
   * @param duration_ms       calibrated full-travel duration in this
   *                          direction. `0` is treated as "uncalibrated"
   *                          and returns `start_position` unchanged.
   * @return position in `[POSITION_MIN, POSITION_MAX]`.
   *
   * Linear estimate, clamped to the legal range. Wraps neither end :
   * crossing 100 while opening (or 0 while closing) clamps to the
   * boundary -- the motor's end-of-travel switch handles the actual
   * stop, our estimate just reflects what the user sees.
   */
  inline uint8_t estimate(uint32_t motion_started_ms,
                          uint32_t now_ms,
                          uint8_t  start_position,
                          int8_t   direction,
                          uint32_t duration_ms) {
    if (direction == DIR_IDLE || duration_ms == 0) return start_position;
    const uint32_t elapsed_ms = (now_ms >= motion_started_ms)
                                ? (now_ms - motion_started_ms)
                                : 0;
    // delta_pct = elapsed / duration * 100, with rounding-down.
    // Compute in 64 bits to avoid overflow on long motions.
    const uint64_t delta_pct_64 =
        (static_cast<uint64_t>(elapsed_ms) * 100u) / duration_ms;
    const int delta_pct = (delta_pct_64 > 100) ? 100
                                               : static_cast<int>(delta_pct_64);
    int new_pos = static_cast<int>(start_position);
    if (direction == DIR_OPENING)      new_pos += delta_pct;
    else if (direction == DIR_CLOSING) new_pos -= delta_pct;
    if (new_pos < POSITION_MIN) new_pos = POSITION_MIN;
    if (new_pos > POSITION_MAX) new_pos = POSITION_MAX;
    return static_cast<uint8_t>(new_pos);
  }

  /**
   * @brief Decide whether the current motion should stop.
   * @param estimated  current live position estimate (from `estimate()`).
   * @param target     desired stop position (0-100).
   * @param direction  current motion direction.
   * @return true if the orchestrator should emit a Stop RF frame now.
   *
   * For intermediate targets (1-99), we trigger Stop as soon as the
   * estimate has reached or passed the target in the motion direction.
   * For 0 / 100, we return false : the motor's end-of-travel switch
   * is more accurate than our estimate, so we let it self-stop and
   * snap our state after the fact.
   */
  inline bool should_stop(uint8_t estimated, uint8_t target, int8_t direction) {
    if (direction == DIR_IDLE) return false;
    if (target == POSITION_MIN || target == POSITION_MAX) return false;
    if (direction == DIR_OPENING) return estimated >= target;
    /* DIR_CLOSING */              return estimated <= target;
  }

  /**
   * @brief Pick the right travel duration for an upcoming motion.
   * @return `open_time_ms` if opening, `close_time_ms` if closing.
   *         Returns `open_time_ms` as a fallback when `close_time_ms`
   *         is 0 (user only calibrated one side ; reasonable default).
   */
  inline uint32_t duration_for(int8_t direction,
                               uint32_t open_time_ms,
                               uint32_t close_time_ms) {
    if (direction == DIR_OPENING) return open_time_ms;
    if (direction == DIR_CLOSING) {
      return (close_time_ms != 0) ? close_time_ms : open_time_ms;
    }
    return 0;
  }

}
