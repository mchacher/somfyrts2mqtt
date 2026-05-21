/**
 * @file orchestrator.cpp
 * @brief Implements the end-to-end command dispatch. See orchestrator.h.
 */
#include "orchestrator.h"

#include <Arduino.h>     // portMUX_TYPE, portENTER_CRITICAL

#include "logger.h"
#include "mqtt.h"
#include "nvs_store.h"
#include "rf.h"

namespace orchestrator {

  // --- Async command queue (web UI -> main loop) -----------------------
  //
  // Calling rf::send_somfy() from an AsyncWebServer worker would block
  // the worker for up to 7 s (Erase PROG 7 s variant). The WiFi stack
  // can drop the HTTP connection during that window and the browser
  // hangs ("button stays disabled indefinitely"). The queue lets the
  // handler return 204 immediately ; the main loop drains and emits.

  namespace {
    struct QueuedCommand {
      uint32_t      remote_id;
      mqtt::Command cmd;
      int           repeat_override;
    };

    constexpr size_t   QUEUE_SIZE = 8;
    QueuedCommand      s_queue[QUEUE_SIZE] = {};
    size_t             s_q_head  = 0;
    size_t             s_q_tail  = 0;
    size_t             s_q_count = 0;
    portMUX_TYPE       s_q_mux   = portMUX_INITIALIZER_UNLOCKED;
  }  // namespace

  bool enqueue_command(uint32_t remote_id, mqtt::Command cmd, int repeat_override) {
    bool ok;
    portENTER_CRITICAL(&s_q_mux);
    if (s_q_count >= QUEUE_SIZE) {
      ok = false;
    } else {
      s_queue[s_q_tail] = QueuedCommand{remote_id, cmd, repeat_override};
      s_q_tail = (s_q_tail + 1) % QUEUE_SIZE;
      ++s_q_count;
      ok = true;
    }
    portEXIT_CRITICAL(&s_q_mux);
    if (!ok) {
      logger::warn("orch", "queue full, dropping cmd for %06X",
                   static_cast<unsigned>(remote_id & 0xFFFFFFu));
    }
    return ok;
  }

  void process_queue() {
    QueuedCommand item;
    bool has_item = false;
    portENTER_CRITICAL(&s_q_mux);
    if (s_q_count > 0) {
      item = s_queue[s_q_head];
      s_q_head = (s_q_head + 1) % QUEUE_SIZE;
      --s_q_count;
      has_item = true;
    }
    portEXIT_CRITICAL(&s_q_mux);

    if (has_item) {
      // Run OUTSIDE the critical section -- emission takes seconds.
      handle_command(item.remote_id, item.cmd, item.repeat_override);
    }
  }

  // --- Synchronous dispatch (MQTT path uses this directly) -------------

  void handle_command(uint32_t remote_id, mqtt::Command cmd, int repeat_override) {
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

    // 4. RF emission. Default repeats:
    //    - PROG = 4 (~800 ms blocking) for pairing tolerance
    //    - others = 1 (~360 ms blocking) for snappy UI
    //    The caller can override (used by the web UI to expose "PROG 3 s" and
    //    "PROG 7 s" long-press variants that put the motor in pair / erase mode).
    const int default_repeat = (cmd == mqtt::Command::Program) ? 4 : 1;
    const int repeat = (repeat_override >= 0) ? repeat_override : default_repeat;
    if (!rf::send_somfy(remote_id, next_code, button, repeat)) {
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
