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

  bool send_somfy(uint32_t remote_id, uint16_t rolling_code, uint8_t button) {
    if (!s_ready) return false;
    if (button == 0) return false;

    // We pass nullptr for RollingCodeStorage: the orchestrator already
    // persisted the rolling code in NVS (persist-before-emit). The lib
    // never dereferences the storage when sendCommandWithCode() is used.
    SomfyRemote remote(CC1101_GDO0, remote_id, nullptr);
    remote.setup();
    remote.sendCommandWithCode(static_cast<::Command>(button), rolling_code, 4);

    logger::info("rf", "tx id=%06X code=%u button=0x%02X",
                 static_cast<unsigned>(remote_id & 0xFFFFFFu),
                 static_cast<unsigned>(rolling_code),
                 static_cast<unsigned>(button));
    return true;
  }

}
