/**
 * @file rf.cpp
 * @brief CC1101 init + status ping (iter 005); send_somfy still stubbed (iter 006).
 */
#include "rf.h"

#include <ELECHOUSE_CC1101_SRC_DRV.h>

#include "config.h"
#include "logger.h"

namespace rf {

  static bool s_ready = false;

  /// CC1101 PARTNUM status register address (expected 0x00 for CC1101).
  static constexpr uint8_t CC1101_REG_PARTNUM = 0x30;
  /// CC1101 VERSION status register address (0x14 typical, 0x04/0x07 on clones).
  static constexpr uint8_t CC1101_REG_VERSION = 0x31;

  bool init() {
    // Step 1: register our custom SPI pinout.
    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CSN);

    // Step 2: Init() must run BEFORE any SpiReadStatus. It is what calls
    // SPI.begin(SCK, MISO, MOSI, -1) with our custom pins + Reset() the chip
    // + writes the default register config. The library's getCC1101() helper
    // and SpiReadStatus() both assume SPI is already started; without Init()
    // they would either read garbage from default ESP pins or noise.
    ELECHOUSE_cc1101.Init();

    // Step 3: read raw status registers for the ping + diagnostic log.
    const uint8_t part    = ELECHOUSE_cc1101.SpiReadStatus(CC1101_REG_PARTNUM);
    const uint8_t version = ELECHOUSE_cc1101.SpiReadStatus(CC1101_REG_VERSION);

    // A working CC1101 reports VERSION in roughly [0x04, 0x18].
    // 0x00 or 0xFF indicate "no chip on the bus".
    const bool ok = (version > 0x00) && (version < 0xFF);
    if (!ok) {
      logger::err("rf",
                  "cc1101 NOT responding (part=0x%02X version=0x%02X) -- check wiring/power",
                  static_cast<unsigned>(part), static_cast<unsigned>(version));
      s_ready = false;
      return false;
    }

    // Step 4: set the Somfy RTS carrier frequency.
    ELECHOUSE_cc1101.setMHZ(SOMFY_FREQ_MHZ);

    s_ready = true;
    logger::info("rf", "cc1101 ok part=0x%02X version=0x%02X freq=%.2f MHz",
                 static_cast<unsigned>(part),
                 static_cast<unsigned>(version),
                 static_cast<double>(SOMFY_FREQ_MHZ));
    return true;
  }

  bool send_somfy(uint32_t remote_id, uint16_t rolling_code, uint8_t button) {
    if (!s_ready) return false;
    // Iter 006 will replace this body with the real Somfy_Remote_Lib emission.
    logger::info("rf", "STUB tx id=%06X code=%u button=0x%02X",
                 static_cast<unsigned>(remote_id & 0xFFFFFFu),
                 static_cast<unsigned>(rolling_code),
                 static_cast<unsigned>(button));
    return true;
  }

}
