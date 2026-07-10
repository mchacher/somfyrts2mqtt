/**
 * @file device_profile.h
 * @brief Per-remote RTS equipment type and its behavioural profile.
 *
 * One orthogonal axis added on top of the existing Shutter model: a remote
 * declares what it drives (`DeviceType`), and the rest of the firmware asks
 * this pure module how to behave — does it track a time-based position, and
 * what wire label does it advertise. Pure header (no NVS/MQTT/Arduino
 * dependency) so it is unit-testable from the native env, mirroring
 * `shutter_state.h` / `ota_guard.h`.
 *
 * Backward compatibility: `DeviceType::Shutter` is value 0, which is also the
 * default a missing NVS key returns, so every pre-existing remote reads as a
 * Shutter and behaves exactly as before.
 */
#pragma once

#include <cstdint>

/**
 * @namespace device_profile
 * @brief Maps a remote's `DeviceType` to how the bridge should treat it.
 */
namespace device_profile {

  /**
   * @brief Kind of RTS equipment a remote drives.
   *
   * Stored as a raw `uint8_t` in NVS. New values may be appended; older
   * firmware degrades an unknown value to `Shutter` via `from_u8()`.
   */
  enum class DeviceType : uint8_t {
    Shutter = 0,  ///< Roller shutter / blind: time-based position 0-100 (default).
    Gate    = 1,  ///< Sliding gate ("portail coulissant"): single-button toggle.
    // Future additions: Pulse, OnOff ...
  };

  // --- Gate toggle button ------------------------------------------------
  //
  // A sequential-mode gate motor cycles (open -> stop -> close -> stop) on a
  // single repeated RTS button. Which button depends on the motor's pairing,
  // so it is configurable per remote. Values are Somfy RTS button codes
  // (upper nibble of frame[1]) so the orchestrator can emit them directly.

  constexpr uint8_t TOGGLE_BTN_MY   = 0x01;  ///< "My"/Stop button.
  constexpr uint8_t TOGGLE_BTN_UP   = 0x02;  ///< Up button (default toggle trigger).
  constexpr uint8_t TOGGLE_BTN_DOWN = 0x04;  ///< Down button.

  /**
   * @brief Clamp a stored toggle-button byte to a valid RTS button.
   * @return the value if it is one of My/Up/Down, else `TOGGLE_BTN_UP` (the
   *         default) — so a fresh remote (NVS default 0) or a corrupt value
   *         resolves to a sensible, working button.
   */
  inline uint8_t valid_toggle_button(uint8_t b) {
    return (b == TOGGLE_BTN_MY || b == TOGGLE_BTN_UP || b == TOGGLE_BTN_DOWN)
               ? b
               : TOGGLE_BTN_UP;
  }

  /**
   * @brief Whether this type is driven by the time-based position estimator.
   * @return true only for `Shutter`. Non-positional types (e.g. `Gate`) are a
   *         binary open/closed cover: no duration calibration, no auto-stop tick.
   */
  inline bool uses_position(DeviceType t) { return t == DeviceType::Shutter; }

  /**
   * @brief Lower-case wire label advertised in the MQTT `Type` hint.
   * @return "shutter" / "gate". Never null.
   */
  inline const char* name(DeviceType t) {
    switch (t) {
      case DeviceType::Gate: return "gate";
      default:               return "shutter";
    }
  }

  /**
   * @brief Decode a raw NVS byte into a `DeviceType`.
   * @param v  stored value.
   * @return the matching type, or `DeviceType::Shutter` for any unknown value
   *         (defensive: a forward-written type degrades safely on older firmware).
   */
  inline DeviceType from_u8(uint8_t v) {
    return (v == static_cast<uint8_t>(DeviceType::Gate)) ? DeviceType::Gate
                                                         : DeviceType::Shutter;
  }

}
