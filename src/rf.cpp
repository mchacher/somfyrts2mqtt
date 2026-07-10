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

  // === iter 022 : 80-bit "Toggle" frame (single-button gate) ===============
  //
  // Somfy's Toggle command lives in an 80-bit RTS frame that the 56-bit
  // Legion2 library cannot build. We encode + bit-bang it ourselves. The frame
  // format and OOK timing mirror rstrouse/ESPSomfy-RTS (the Somfy RTS wire
  // protocol; SYMBOL = 640 us, wake-up pulse, 4-SYMBOL hardware-sync pulses,
  // a software sync, then Manchester data MSB-first).

  namespace {
    constexpr int SOMFY_SYMBOL_US = 640;

    /// 80-bit extension checksum over the 3 trailing bytes (nibble XOR).
    uint8_t calc80_checksum(uint8_t b7, uint8_t b8, uint8_t b9) {
      uint8_t cs = static_cast<uint8_t>(((b7 & 0xF0) >> 4) ^ ((b8 & 0xF0) >> 4));
      cs ^= static_cast<uint8_t>((b9 & 0xF0) >> 4);
      cs ^= static_cast<uint8_t>(b7 & 0x0F);
      cs ^= static_cast<uint8_t>(b8 & 0x0F);
      return static_cast<uint8_t>(cs & 0x0F);
    }

    /// frame[7] encodes the transmission index; wraps like ESPSomfy.
    uint8_t encode80_byte7(uint8_t start, int repeat) {
      while ((repeat * 4) + start > 255) repeat -= 15;
      return static_cast<uint8_t>(start + (repeat * 4));
    }

    /// Set the 3 plain (non-obfuscated) extension bytes for the Toggle command.
    void set_toggle_ext(uint8_t* f, int tx_index) {
      f[7] = encode80_byte7(196, tx_index);
      f[8] = 0x00;
      f[9] = static_cast<uint8_t>(0x10 | calc80_checksum(f[7], f[8], f[9]));
    }

    /// Bit-bang one 80-bit frame on GDO0 (carrier keyed by the CC1101 in OOK).
    void tx_frame_80(const uint8_t* f, int hw_sync, bool wakeup) {
      if (wakeup) {
        digitalWrite(CC1101_GDO0, HIGH); delayMicroseconds(10920);
        digitalWrite(CC1101_GDO0, LOW);  delayMicroseconds(7357);
      }
      for (int i = 0; i < hw_sync; i++) {
        digitalWrite(CC1101_GDO0, HIGH); delayMicroseconds(4 * SOMFY_SYMBOL_US);
        digitalWrite(CC1101_GDO0, LOW);  delayMicroseconds(4 * SOMFY_SYMBOL_US);
      }
      // Software sync.
      digitalWrite(CC1101_GDO0, HIGH); delayMicroseconds(4850);
      digitalWrite(CC1101_GDO0, LOW);  delayMicroseconds(SOMFY_SYMBOL_US);
      // 80 Manchester data bits, MSB first. 1 = low-then-high, 0 = high-then-low.
      int last = 0;
      for (int i = 0; i < 80; i++) {
        const int bit = (f[i / 8] >> (7 - (i % 8))) & 1;
        if (bit) {
          digitalWrite(CC1101_GDO0, LOW);  delayMicroseconds(SOMFY_SYMBOL_US);
          digitalWrite(CC1101_GDO0, HIGH); delayMicroseconds(SOMFY_SYMBOL_US);
          last = 1;
        } else {
          digitalWrite(CC1101_GDO0, HIGH); delayMicroseconds(SOMFY_SYMBOL_US);
          digitalWrite(CC1101_GDO0, LOW);  delayMicroseconds(SOMFY_SYMBOL_US);
          last = 0;
        }
      }
      if (last == 0) digitalWrite(CC1101_GDO0, HIGH);  // terminate a trailing 0
      digitalWrite(CC1101_GDO0, LOW);
    }
  }  // namespace

  bool send_toggle(uint32_t remote_id, uint16_t rolling_code, int repeat) {
    if (!s_ready) return false;

    // Constant part frame[0..6] (0xA4 / 0xF0|checksum / rolling code / id),
    // then the classic checksum + obfuscation. Bytes 7-9 stay plain and are
    // (re)written per transmission below.
    uint8_t f[10] = {0};
    f[0] = 0xA4;                                             // Toggle key
    f[1] = 0xF0;                                             // RTWProto marker (low nibble = checksum)
    f[2] = static_cast<uint8_t>(rolling_code >> 8);
    f[3] = static_cast<uint8_t>(rolling_code & 0xFF);
    f[4] = static_cast<uint8_t>((remote_id >> 16) & 0xFF);
    f[5] = static_cast<uint8_t>((remote_id >> 8) & 0xFF);
    f[6] = static_cast<uint8_t>(remote_id & 0xFF);
    uint8_t checksum = 0;
    for (int i = 0; i < 7; i++)
      checksum = static_cast<uint8_t>(checksum ^ f[i] ^ (f[i] >> 4));
    f[1] |= static_cast<uint8_t>(checksum & 0x0F);
    for (int i = 1; i < 7; i++) f[i] ^= f[i - 1];            // obfuscation (bytes 0-6)

    // First frame carries a wake-up pulse + 12 hw-sync pulses; repeats get 6.
    set_toggle_ext(f, 0);
    tx_frame_80(f, 12, true);
    for (int r = 1; r <= repeat; r++) {
      set_toggle_ext(f, r);
      tx_frame_80(f, 6, false);
    }

    logger::info("rf", "tx TOGGLE(80b) id=%06X code=%u repeat=%d",
                 static_cast<unsigned>(remote_id & 0xFFFFFFu),
                 static_cast<unsigned>(rolling_code), repeat);
    return true;
  }

}
