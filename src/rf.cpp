/**
 * @file rf.cpp
 * @brief Iter 004 stub implementation of the rf module.
 *
 * Real CC1101 init / SPI / OOK emission lands in iter 005 + 006.
 * For now both functions only log; the orchestrator chain can be
 * validated end-to-end with this stub.
 */
#include "rf.h"
#include "logger.h"

namespace rf {

  static bool s_ready = false;

  bool init() {
    s_ready = true;
    logger::info("rf", "stub init (CC1101 not wired yet)");
    return true;
  }

  bool send_somfy(uint32_t remote_id, uint16_t rolling_code, uint8_t button) {
    if (!s_ready) return false;
    logger::info("rf", "STUB tx id=%06X code=%u button=0x%02X",
                 static_cast<unsigned>(remote_id & 0xFFFFFFu),
                 static_cast<unsigned>(rolling_code),
                 static_cast<unsigned>(button));
    return true;
  }

}
