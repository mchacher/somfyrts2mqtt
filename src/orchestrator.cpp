/**
 * @file orchestrator.cpp
 * @brief Implements the end-to-end command dispatch + the iter 014
 *        shutter position state machine. See orchestrator.h.
 */
#include "orchestrator.h"

#include <Arduino.h>     // portMUX_TYPE, portENTER_CRITICAL, millis
#include <cstring>

#include "logger.h"
#include "mqtt.h"
#include "nvs_store.h"
#include "rf.h"
#include "shutter_state.h"

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

  // --- Shutter position state machine (iter 014) -----------------------

  namespace {
    struct RuntimeEntry {
      uint32_t remote_id;          ///< 0 = empty slot.
      uint8_t  position;           ///< Live 0-100.
      int8_t   direction;          ///< -1 closing, 0 idle, +1 opening.
      uint8_t  target;             ///< 0-100, destination.
      uint32_t motion_started_ms;  ///< millis() at RF emission of Up/Down.
      uint8_t  start_position;     ///< Position at motion_started_ms.
    };

    RuntimeEntry s_runtimes[nvs_store::MAX_REMOTES] = {};

    /// Find the runtime entry for @p remote_id, or `nullptr` if absent.
    RuntimeEntry* find_runtime(uint32_t remote_id) {
      for (auto& rt : s_runtimes) {
        if (rt.remote_id == remote_id) return &rt;
      }
      return nullptr;
    }

    /// Either find or allocate a free slot for @p remote_id.
    RuntimeEntry* upsert_runtime(uint32_t remote_id) {
      RuntimeEntry* rt = find_runtime(remote_id);
      if (rt != nullptr) return rt;
      for (auto& slot : s_runtimes) {
        if (slot.remote_id == 0) {
          slot.remote_id = remote_id;
          return &slot;
        }
      }
      return nullptr;
    }

    /// Build the JSON ack `{"Position":N,"Direction":D,"Target":T}` for one remote.
    void publish_stat(const nvs_store::Remote& remote, const RuntimeEntry& rt) {
      mqtt::publish_shutter_state(remote.name.c_str(),
                                  rt.position, rt.direction, rt.target);
    }
  }  // namespace

  void init_runtimes() {
    std::memset(s_runtimes, 0, sizeof(s_runtimes));
    nvs_store::Remote buf[nvs_store::MAX_REMOTES];
    const size_t n = nvs_store::list_remotes(buf, nvs_store::MAX_REMOTES);
    for (size_t i = 0; i < n; ++i) {
      RuntimeEntry& rt = s_runtimes[i];
      rt.remote_id      = buf[i].id;
      rt.position       = buf[i].position;
      rt.direction      = shutter_state::DIR_IDLE;
      rt.target         = buf[i].position;
      rt.start_position = buf[i].position;
    }
    logger::info("orch", "init_runtimes : %u remote(s) cached",
                 static_cast<unsigned>(n));
  }

  // --- Synchronous dispatch (MQTT + queue both end here) ----------------

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

    // 4. RF emission.
    const int default_repeat = (cmd == mqtt::Command::Program) ? 4 : 1;
    const int repeat = (repeat_override >= 0) ? repeat_override : default_repeat;
    if (!rf::send_somfy(remote_id, next_code, button, repeat)) {
      logger::err("orch", "rf::send_somfy failed for %06X",
                  static_cast<unsigned>(remote_id & 0xFFFFFFu));
      return;
    }

    // 5. Update the runtime state machine (iter 014).
    RuntimeEntry* rt = upsert_runtime(remote_id);
    if (rt != nullptr) {
      const uint32_t now = millis();
      if (cmd == mqtt::Command::Up) {
        rt->start_position    = rt->position;
        rt->motion_started_ms = now;
        rt->direction         = shutter_state::DIR_OPENING;
        rt->target            = 100;
      } else if (cmd == mqtt::Command::Down) {
        rt->start_position    = rt->position;
        rt->motion_started_ms = now;
        rt->direction         = shutter_state::DIR_CLOSING;
        rt->target            = 0;
      } else if (cmd == mqtt::Command::Stop) {
        // Freeze the live estimate, then transition to idle and persist
        // the snapshot (the only NVS write on the position field per
        // motion -- keeps flash wear bounded).
        if (rt->direction != shutter_state::DIR_IDLE) {
          const uint32_t duration =
              shutter_state::duration_for(rt->direction,
                                          remote.open_time_ms,
                                          remote.close_time_ms);
          rt->position = shutter_state::estimate(rt->motion_started_ms, now,
                                                 rt->start_position,
                                                 rt->direction, duration);
        }
        rt->direction = shutter_state::DIR_IDLE;
        nvs_store::update_position(remote_id, rt->position);
      }
      // 6. Publish per-remote ack + aggregated sensor.
      publish_stat(remote, *rt);
      mqtt::publish_sensor_aggregated();
    }

    logger::info("orch", "executed %s on %s code=%u",
                 mqtt::command_to_str(cmd),
                 remote.name.c_str(),
                 static_cast<unsigned>(next_code));
  }

  // --- Shutter-specific entry points (iter 014) -------------------------

  void set_position(uint32_t remote_id, uint8_t target) {
    if (target > 100) target = 100;

    nvs_store::Remote remote;
    if (!nvs_store::get_remote(remote_id, remote)) {
      logger::warn("orch", "set_position : unknown remote %06X",
                   static_cast<unsigned>(remote_id & 0xFFFFFFu));
      return;
    }
    RuntimeEntry* rt = upsert_runtime(remote_id);
    if (rt == nullptr) {
      logger::warn("orch", "set_position : runtime table full");
      return;
    }

    if (target == rt->position && rt->direction == shutter_state::DIR_IDLE) {
      // No-op : already there. Still ack so the caller sees a stat update.
      publish_stat(remote, *rt);
      return;
    }

    const int8_t direction = (target > rt->position)
                             ? shutter_state::DIR_OPENING
                             : shutter_state::DIR_CLOSING;

    // Intermediate targets require a calibrated duration to know when to
    // emit Stop. Extreme targets (0 / 100) are fine without calibration --
    // the motor's end-of-travel switch is the authoritative stop.
    if (target > 0 && target < 100) {
      const uint32_t duration =
          shutter_state::duration_for(direction,
                                      remote.open_time_ms,
                                      remote.close_time_ms);
      if (duration == 0) {
        logger::warn("orch",
                     "set_position : %s not calibrated, refusing target %u",
                     remote.name.c_str(), target);
        mqtt::publish_shutter_error(remote.name.c_str(), "not calibrated");
        return;
      }
    }

    // Dispatch through handle_command for the RF + NVS + log book-keeping,
    // then override the runtime target (handle_command always sets 0 or 100
    // for Up / Down).
    const mqtt::Command cmd = (direction == shutter_state::DIR_OPENING)
                              ? mqtt::Command::Up
                              : mqtt::Command::Down;
    handle_command(remote_id, cmd);
    rt->target = target;
    publish_stat(remote, *rt);
  }

  void set_calibration_position(uint32_t remote_id, uint8_t position) {
    if (position > 100) position = 100;

    nvs_store::Remote remote;
    if (!nvs_store::get_remote(remote_id, remote)) {
      logger::warn("orch", "set_calibration_position : unknown remote %06X",
                   static_cast<unsigned>(remote_id & 0xFFFFFFu));
      return;
    }
    RuntimeEntry* rt = upsert_runtime(remote_id);
    if (rt == nullptr) return;

    // Manual sync : no RF, just update both the live cache and the NVS
    // snapshot. Aborts any current motion (the new position is now the
    // ground truth, so direction is reset).
    rt->position       = position;
    rt->target         = position;
    rt->direction      = shutter_state::DIR_IDLE;
    rt->start_position = position;
    nvs_store::update_position(remote_id, position);

    logger::info("orch", "set_calibration_position %s -> %u",
                 remote.name.c_str(), position);
    publish_stat(remote, *rt);
    mqtt::publish_sensor_aggregated();
  }

  void set_open_duration(uint32_t remote_id, uint32_t open_time_ms) {
    nvs_store::Remote remote;
    if (!nvs_store::get_remote(remote_id, remote)) return;
    if (!nvs_store::set_open_duration(remote_id, open_time_ms)) return;
    logger::info("orch", "set_open_duration %s = %u ms",
                 remote.name.c_str(), static_cast<unsigned>(open_time_ms));
    const nvs_store::Remote refreshed = [&]() {
      nvs_store::Remote r{};
      nvs_store::get_remote(remote_id, r);
      return r;
    }();
    mqtt::publish_shutter_duration(remote.name.c_str(),
                                   refreshed.open_time_ms,
                                   refreshed.close_time_ms);
  }

  void set_close_duration(uint32_t remote_id, uint32_t close_time_ms) {
    nvs_store::Remote remote;
    if (!nvs_store::get_remote(remote_id, remote)) return;
    if (!nvs_store::set_close_duration(remote_id, close_time_ms)) return;
    logger::info("orch", "set_close_duration %s = %u ms",
                 remote.name.c_str(), static_cast<unsigned>(close_time_ms));
    const nvs_store::Remote refreshed = [&]() {
      nvs_store::Remote r{};
      nvs_store::get_remote(remote_id, r);
      return r;
    }();
    mqtt::publish_shutter_duration(remote.name.c_str(),
                                   refreshed.open_time_ms,
                                   refreshed.close_time_ms);
  }

  RuntimeState get_runtime(uint32_t remote_id) {
    RuntimeState s{};
    const RuntimeEntry* rt = find_runtime(remote_id);
    if (rt != nullptr) {
      s.position  = rt->position;
      s.direction = rt->direction;
      s.target    = rt->target;
    }
    return s;
  }

  void tick(uint32_t now_ms) {
    bool anything_moved = false;
    for (auto& rt : s_runtimes) {
      if (rt.remote_id == 0) continue;
      if (rt.direction == shutter_state::DIR_IDLE) continue;

      nvs_store::Remote remote;
      if (!nvs_store::get_remote(rt.remote_id, remote)) continue;

      const uint32_t duration =
          shutter_state::duration_for(rt.direction,
                                      remote.open_time_ms,
                                      remote.close_time_ms);
      const uint8_t estimated =
          shutter_state::estimate(rt.motion_started_ms, now_ms,
                                  rt.start_position, rt.direction, duration);
      rt.position = estimated;
      anything_moved = true;

      // Decide whether the motion ends here.
      if (rt.target == 0 || rt.target == 100) {
        // Let the motor self-stop. We snap and idle when our estimate
        // catches the end of travel.
        const bool reached = (rt.direction == shutter_state::DIR_OPENING
                              ? estimated >= 100
                              : estimated <= 0);
        if (reached) {
          rt.position  = (rt.direction == shutter_state::DIR_OPENING) ? 100 : 0;
          rt.direction = shutter_state::DIR_IDLE;
          nvs_store::update_position(rt.remote_id, rt.position);
          publish_stat(remote, rt);
        }
      } else {
        // Intermediate target : emit Stop when we cross it. The Stop is
        // enqueued because tick() runs in the main loop -- the actual RF
        // emission happens via process_queue() in the next tick.
        if (shutter_state::should_stop(estimated, rt.target, rt.direction)) {
          enqueue_command(rt.remote_id, mqtt::Command::Stop, -1);
          // Don't transition direction here ; handle_command(Stop) will,
          // and it will publish stat + sensor too.
        }
      }
    }

    if (anything_moved) {
      mqtt::publish_sensor_aggregated();
    }
  }

}
