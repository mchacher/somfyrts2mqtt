/**
 * @file orchestrator.cpp
 * @brief Implements the end-to-end command dispatch. See orchestrator.h.
 */
#include "orchestrator.h"

#include "logger.h"
#include "mqtt.h"
#include "nvs_store.h"
#include "rf.h"

namespace orchestrator {

  void handle_command(uint32_t remote_id, mqtt::Command cmd) {
    // 1. Look up the remote in NVS.
    nvs_store::Remote remote;
    if (!nvs_store::get_remote(remote_id, remote)) {
      logger::warn("orch", "unknown remote %06X, drop",
                   static_cast<unsigned>(remote_id & 0xFFFFFFu));
      return;
    }

    // 2. Translate the MQTT command to a Somfy button bitmap.
    const uint8_t button = command_to_button(cmd);
    if (button == 0) {
      // Should not happen — mqtt drops Invalid before calling us.
      logger::warn("orch", "invalid command for %06X, drop",
                   static_cast<unsigned>(remote_id & 0xFFFFFFu));
      return;
    }

    // 3. Increment + persist BEFORE the RF emission. If we ever crash
    //    between persist and emit, we lose one code at worst, but the
    //    Somfy receiver never sees a replayed code.
    const uint16_t next_code = static_cast<uint16_t>(remote.rolling_code + 1);
    if (!nvs_store::update_rolling_code(remote_id, next_code)) {
      logger::err("orch", "failed to persist rolling code for %06X",
                  static_cast<unsigned>(remote_id & 0xFFFFFFu));
      return;
    }

    // 4. RF emission (stub for iter 004; real CC1101 emission in iter 006).
    if (!rf::send_somfy(remote_id, next_code, button)) {
      logger::err("orch", "rf::send_somfy failed for %06X",
                  static_cast<unsigned>(remote_id & 0xFFFFFFu));
      return;
    }

    // 5. Publish retained state messages so consumers can sync after a
    //    reconnect. publish_* may return false if MQTT is mid-reconnect;
    //    we log but do not abort.
    if (!mqtt::publish_state(remote_id, cmd)) {
      logger::warn("orch", "publish_state dropped (not connected)");
    }
    if (!mqtt::publish_rolling_code(remote_id, next_code)) {
      logger::warn("orch", "publish_rolling_code dropped (not connected)");
    }

    logger::info("orch", "executed %s on %06X code=%u",
                 mqtt::command_to_str(cmd),
                 static_cast<unsigned>(remote_id & 0xFFFFFFu),
                 static_cast<unsigned>(next_code));
  }

}
