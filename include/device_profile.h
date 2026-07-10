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

  /// Somfy RTS "Toggle" command (button nibble 0x0C). A single-button gate
  /// motor cycles open → stop → close → stop on this one command — it is a
  /// dedicated RTS code, distinct from Up/Down/My (which do those actions
  /// separately). Matches `somfy_commands::Toggle` in rstrouse/ESPSomfy-RTS.
  constexpr uint8_t SOMFY_TOGGLE = 0x0C;

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
