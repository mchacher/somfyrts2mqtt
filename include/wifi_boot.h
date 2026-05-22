/**
 * @file wifi_boot.h
 * @brief Pure-logic boot counter for the 4-power-cycles AP-recovery.
 *
 * Pattern from Tasmota: increment a small counter at every boot, reset it
 * once the firmware has been running stably for a few seconds. If the user
 * power-cycles the box N times in quick succession (faster than the stable
 * window), the counter reaches the threshold and we force AP mode for one
 * boot to let the user re-enter credentials.
 *
 * Header-only and Arduino-free so that the native test environment can
 * exercise the math without flashing.
 */
#pragma once

#include <cstdint>

namespace wifi_boot {

  /**
   * @brief Compute the new counter value at boot.
   * @param stored_count value read from NVS at boot.
   * @param threshold    counter value that triggers AP forcing (typically 4).
   * @return the value to persist to NVS this boot.
   *
   * Saturating increment: once at or above @p threshold, stay there until the
   * caller explicitly resets. This guarantees `should_force_ap` keeps firing
   * even if a write to NVS got dropped between the increment and the AP
   * branch.
   */
  inline uint8_t on_boot(uint8_t stored_count, uint8_t threshold) {
    if (stored_count >= threshold) return threshold;
    return static_cast<uint8_t>(stored_count + 1);
  }

  /**
   * @brief Whether the post-increment counter has reached the AP threshold.
   * @param post_increment_count what `on_boot()` returned.
   * @param threshold same value as passed to `on_boot()`.
   */
  inline bool should_force_ap(uint8_t post_increment_count, uint8_t threshold) {
    return post_increment_count >= threshold;
  }

  /**
   * @brief Whether the caller should persist a `count = 0` write now.
   * @param stored_count current counter value (cached in RAM).
   * @param uptime_ms    milliseconds since boot.
   * @param stable_ms    uptime threshold (typically 5000 ms).
   * @return true iff @p stored_count is non-zero AND @p uptime_ms has reached
   *         @p stable_ms. Caller writes 0 to NVS and updates the cache.
   *
   * The non-zero gate avoids a redundant NVS write every loop iteration. The
   * uptime gate is the "the box has booted normally" signal.
   */
  inline bool should_reset(uint8_t stored_count,
                           uint32_t uptime_ms,
                           uint32_t stable_ms) {
    return (stored_count > 0) && (uptime_ms >= stable_ms);
  }

}
