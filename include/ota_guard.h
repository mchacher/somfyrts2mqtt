/**
 * @file ota_guard.h
 * @brief Compile-time chip identity + ESP image header parsing for safe OTA.
 *
 * With more than one board variant shipping distinct, non-interchangeable
 * binaries (RISC-V C3 vs Xtensa S3), the WebOTA path must reject an image
 * built for another chip before writing it. This pure header exposes the
 * running chip's expected id and a parser for the incoming image header, so
 * the check is unit-testable from the native env (no Arduino / Update
 * dependency), mirroring `include/shutter_state.h`.
 */
#pragma once

#include <cstddef>
#include <cstdint>

/**
 * @namespace ota_guard
 * @brief Reject an OTA image that targets a different ESP chip.
 *
 * The `esp_image_header` sits at offset 0 of every ESP app image: byte 0 is
 * the magic `0xE9`, and `chip_id` (uint16, little-endian) is at offset 12.
 * The first WebOTA chunk is far larger than the header, so the caller can
 * validate before ever touching the flash.
 */
namespace ota_guard {

  /// ESP application image magic byte (`esp_image_header.magic`), offset 0.
  constexpr uint8_t IMAGE_MAGIC = 0xE9;

  /// Byte offset of the little-endian uint16 `chip_id` in `esp_image_header`.
  constexpr size_t CHIP_ID_OFFSET = 12;

  /// Minimum prefix length needed to read the `chip_id` (offset 12 + 2 bytes).
  constexpr size_t MIN_HEADER_LEN = CHIP_ID_OFFSET + 2;

  /// `esp_chip_id_t` values we can ship for (from esp_app_format.h).
  constexpr uint16_t CHIP_ID_ESP32C3 = 0x0005;
  constexpr uint16_t CHIP_ID_ESP32S3 = 0x0009;

  /// Chip id this firmware was compiled for. Set from `CONFIG_IDF_TARGET_*`.
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  constexpr uint16_t EXPECTED_CHIP_ID = CHIP_ID_ESP32C3;
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
  constexpr uint16_t EXPECTED_CHIP_ID = CHIP_ID_ESP32S3;
#else
  // native / host build: no real target. Tests pass an explicit id, so this
  // placeholder is never used by the (hardware-only) OTA path.
  constexpr uint16_t EXPECTED_CHIP_ID = 0xFFFF;
#endif

  /**
   * @brief Human-readable name for an `esp_chip_id_t` value.
   * @param id  chip id (e.g. from `header_chip_id()` or `EXPECTED_CHIP_ID`).
   * @return "ESP32-C3" / "ESP32-S3", or "another chip" for anything else.
   */
  inline const char* chip_name(uint16_t id) {
    switch (id) {
      case CHIP_ID_ESP32C3: return "ESP32-C3";
      case CHIP_ID_ESP32S3: return "ESP32-S3";
      default:              return "another chip";
    }
  }

  /// Sentinel returned by `header_chip_id()` when the buffer is not a
  /// readable ESP image header. A valid chip_id is 0..0xFFFF, so -1 is unused.
  constexpr int32_t CHIP_ID_INVALID = -1;

  /**
   * @brief Extract the `chip_id` from an ESP image header prefix.
   * @param buf  first bytes of the firmware image (the OTA first chunk).
   * @param len  number of valid bytes in @p buf.
   * @return the chip_id (0..0xFFFF), or `CHIP_ID_INVALID` (-1) if @p buf is
   *         null, shorter than `MIN_HEADER_LEN`, or does not start with
   *         `IMAGE_MAGIC` (i.e. not an ESP image).
   *
   * @note Returns an `int32_t` sentinel rather than `std::optional` so the
   *       header compiles under the Arduino toolchain's C++ standard, which
   *       is not guaranteed to be C++17 (the `native` test env is; the arduino
   *       firmware env inherits the framework default).
   */
  inline int32_t header_chip_id(const uint8_t* buf, size_t len) {
    if (buf == nullptr || len < MIN_HEADER_LEN) return CHIP_ID_INVALID;
    if (buf[0] != IMAGE_MAGIC) return CHIP_ID_INVALID;
    return static_cast<int32_t>(buf[CHIP_ID_OFFSET] |
                                (buf[CHIP_ID_OFFSET + 1] << 8));
  }

}
