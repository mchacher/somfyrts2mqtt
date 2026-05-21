/**
 * @file rf.cpp
 * @brief CC1101 init + real Somfy RTS emission. See rf.h.
 */
#include "rf.h"

#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <SomfyRemote.h>

#include "config.h"
#include "logger.h"
#include "rts_frame.h"

namespace rf {

  static bool s_ready = false;

  /// CC1101 PARTNUM status register address (expected 0x00 for CC1101).
  static constexpr uint8_t CC1101_REG_PARTNUM = 0x30;
  /// CC1101 VERSION status register address (0x14 typical, 0x04 / 0x07 on clones).
  static constexpr uint8_t CC1101_REG_VERSION = 0x31;

  /// CC1101 PA power in dBm. Somfy remotes are ~+9 dBm; +10 gives a small edge.
  static constexpr uint8_t CC1101_PA_DBM = 10;

  /// CC1101 modulation index for ASK/OOK (per SmartRC-CC1101 lib mapping).
  static constexpr uint8_t CC1101_MOD_OOK = 2;

  /// CC1101 PKT_FORMAT for asynchronous serial mode (carrier gated by GDO0 level).
  static constexpr uint8_t CC1101_PKT_FORMAT_ASYNC = 3;

  bool init() {
    // Step 1: register our custom SPI pinout.
    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CSN);

    // Step 2: Init() must run BEFORE any SpiReadStatus call. It is what calls
    // SPI.begin(SCK, MISO, MOSI, -1) with our custom pins + Reset() the chip
    // + writes the default register config.
    ELECHOUSE_cc1101.Init();

    // Step 3: read raw status registers for the ping + diagnostic log.
    const uint8_t part    = ELECHOUSE_cc1101.SpiReadStatus(CC1101_REG_PARTNUM);
    const uint8_t version = ELECHOUSE_cc1101.SpiReadStatus(CC1101_REG_VERSION);
    const bool ok = (version > 0x00) && (version < 0xFF);
    if (!ok) {
      logger::err("rf",
                  "cc1101 NOT responding (part=0x%02X version=0x%02X) -- check wiring/power",
                  static_cast<unsigned>(part), static_cast<unsigned>(version));
      s_ready = false;
      return false;
    }
    logger::info("rf", "cc1101 ok part=0x%02X version=0x%02X",
                 static_cast<unsigned>(part), static_cast<unsigned>(version));

    // Step 4: configure for Somfy RTS OOK emission.
    //   - 433.42 MHz carrier
    //   - ASK/OOK modulation
    //   - PA +10 dBm
    //   - No preamble / sync word (we drive the raw RF level via GDO0)
    //   - PKT_FORMAT = 3 (async serial) -> CC1101 keys the carrier from GDO0
    ELECHOUSE_cc1101.setMHZ(SOMFY_FREQ_MHZ);
    ELECHOUSE_cc1101.setModulation(CC1101_MOD_OOK);
    ELECHOUSE_cc1101.setPA(CC1101_PA_DBM);
    ELECHOUSE_cc1101.setSyncMode(0);
    ELECHOUSE_cc1101.setPktFormat(CC1101_PKT_FORMAT_ASYNC);
    ELECHOUSE_cc1101.SetTx();

    // Step 5: GDO0 is the modulation input -- we drive it directly with
    // digitalWrite() during emission. Start at LOW (no carrier).
    pinMode(CC1101_GDO0, OUTPUT);
    digitalWrite(CC1101_GDO0, LOW);

    s_ready = true;
    logger::info("rf",
                 "cc1101 OOK ready freq=%.2f MHz pa=%u dBm mod=%u pkt_format=%u",
                 static_cast<double>(SOMFY_FREQ_MHZ),
                 static_cast<unsigned>(CC1101_PA_DBM),
                 static_cast<unsigned>(CC1101_MOD_OOK),
                 static_cast<unsigned>(CC1101_PKT_FORMAT_ASYNC));
    return true;
  }

  bool send_somfy(uint32_t remote_id, uint16_t rolling_code, uint8_t button, int repeat) {
    if (!s_ready) return false;
    if (button == 0) return false;

    // We pass nullptr for RollingCodeStorage: the orchestrator already
    // persisted the rolling code in NVS (persist-before-emit). The lib
    // never dereferences the storage when sendCommandWithCode() is used.
    SomfyRemote remote(CC1101_GDO0, remote_id, nullptr);
    remote.setup();
    remote.sendCommandWithCode(static_cast<::Command>(button), rolling_code, repeat);

    logger::info("rf", "tx id=%06X code=%u button=0x%02X repeat=%d",
                 static_cast<unsigned>(remote_id & 0xFFFFFFu),
                 static_cast<unsigned>(rolling_code),
                 static_cast<unsigned>(button),
                 repeat);
    return true;
  }

  // --- Long-press path (Pair 3 s / Erase 7 s) -----------------------------
  //
  // Reimplements the bit-bang sender locally with a tight inter-frame gap
  // (3 ms instead of the lib's 30 ms). That is the only difference vs the
  // Legion2 lib at the wire level -- frame layout matches byte-for-byte
  // (verified in test/test_rts_frame).

  /// Manchester half-period (µs). Same value as Legion2 / Pushstack ref.
  static constexpr uint16_t SYMBOL_US = 640;
  /// Wake-up pulse high duration (µs). Lib-matched.
  static constexpr uint16_t WAKEUP_HIGH_US = 9415;
  /// Wake-up pulse low duration (µs). Lib-matched.
  static constexpr uint16_t WAKEUP_LOW_US = 9565;
  /// Silence between the wake-up pulse and the first frame (ms).
  static constexpr uint16_t WAKEUP_SILENCE_MS = 80;
  /// Hardware sync pulses on the first (initial) frame.
  static constexpr uint8_t HW_SYNC_FIRST = 2;
  /// Hardware sync pulses on every repeated frame.
  static constexpr uint8_t HW_SYNC_REPEAT = 7;
  /// Software sync high level duration (µs).
  static constexpr uint16_t SW_SYNC_HIGH_US = 4550;
  /// End-of-frame silence (µs). Caller-side inter-frame gap follows.
  static constexpr uint16_t INTER_FRAME_SILENCE_US = 415;
  /// Gap between two consecutive frames (µs). 3 000 keeps the data-to-data
  /// gap under the motor's "press release" threshold; vs the Legion2 lib's
  /// 30 000 that makes long-press detection unreliable.
  static constexpr uint16_t INTER_FRAME_GAP_US = 3000;
  /// Number of data bits per frame (7 bytes * 8).
  static constexpr uint8_t FRAME_BITS = 56;

  static inline void rf_high_us(uint16_t us) {
    digitalWrite(CC1101_GDO0, HIGH);
    delayMicroseconds(us);
  }
  static inline void rf_low_us(uint16_t us) {
    digitalWrite(CC1101_GDO0, LOW);
    delayMicroseconds(us);
  }

  /**
   * @brief Emit a single Somfy RTS frame on GDO0.
   * @param frame   Obfuscated 7-byte frame.
   * @param sync    Number of hardware-sync pulses to prepend.
   * @param wakeup  True only for the very first frame of a command.
   *
   * Runs entirely under `noInterrupts()` because every pulse is timed
   * with `delayMicroseconds()`. On the ESP32-C3 (single core) this
   * means WiFi events stall during the ~110 ms data window; the main
   * loop's 3 ms inter-frame gap is the breathing room.
   */
  static void send_frame_longpress(const uint8_t frame[rts_frame::SIZE],
                                   uint8_t sync, bool wakeup) {
    if (wakeup) {
      noInterrupts();
      rf_high_us(WAKEUP_HIGH_US);
      rf_low_us(WAKEUP_LOW_US);
      interrupts();
      delay(WAKEUP_SILENCE_MS);
    }

    noInterrupts();

    // Hardware sync: `sync` pairs of (4*SYMBOL high, 4*SYMBOL low).
    for (uint8_t i = 0; i < sync; ++i) {
      rf_high_us(static_cast<uint16_t>(4 * SYMBOL_US));
      rf_low_us(static_cast<uint16_t>(4 * SYMBOL_US));
    }

    // Software sync.
    rf_high_us(SW_SYNC_HIGH_US);
    rf_low_us(SYMBOL_US);

    // Data: 56 bits, Manchester-encoded with SYMBOL_US half-period.
    //   bit = 1 -> low then high
    //   bit = 0 -> high then low
    for (uint8_t i = 0; i < FRAME_BITS; ++i) {
      const uint8_t byte_idx = static_cast<uint8_t>(i / 8);
      const uint8_t bit_idx  = static_cast<uint8_t>(7 - (i % 8));
      const bool bit_one = ((frame[byte_idx] >> bit_idx) & 1) == 1;
      if (bit_one) {
        rf_low_us(SYMBOL_US);
        rf_high_us(SYMBOL_US);
      } else {
        rf_high_us(SYMBOL_US);
        rf_low_us(SYMBOL_US);
      }
    }

    // End-of-frame silence (before the caller-side inter-frame gap).
    rf_low_us(INTER_FRAME_SILENCE_US);

    interrupts();
  }

  bool send_somfy_longpress(uint32_t remote_id, uint16_t rolling_code,
                            uint8_t button, int repeat) {
    if (!s_ready) return false;
    if (button == 0) return false;
    if (repeat < 0) repeat = 0;

    uint8_t frame[rts_frame::SIZE];
    rts_frame::build_frame(button, rolling_code, remote_id, frame);

    send_frame_longpress(frame, HW_SYNC_FIRST, /*wakeup*/ true);
    for (int i = 0; i < repeat; ++i) {
      delayMicroseconds(INTER_FRAME_GAP_US);
      send_frame_longpress(frame, HW_SYNC_REPEAT, /*wakeup*/ false);
    }

    logger::info("rf",
                 "tx-lp id=%06X code=%u button=0x%02X repeat=%d gap=%uus",
                 static_cast<unsigned>(remote_id & 0xFFFFFFu),
                 static_cast<unsigned>(rolling_code),
                 static_cast<unsigned>(button),
                 repeat,
                 static_cast<unsigned>(INTER_FRAME_GAP_US));
    return true;
  }

}
