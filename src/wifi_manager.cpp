/**
 * @file wifi_manager.cpp
 * @brief WiFi manager: boot decision tree + captive portal + scan/pin BSSID.
 *        See wifi_manager.h and specs/015-wifi-captive-portal/.
 */
#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>       // mDNS responder for <hostname>.local
#include <ArduinoOTA.h>    // dev-workflow OTA over espota (iter 016)
#include <cstdio>
#include <cstring>

#include "captive_portal.h"  // spec 018: replaces tzapu/WiFiManager
#include "config.h"
#include "logger.h"
#include "nvs_store.h"
#include "secrets.h"
#include "wifi_boot.h"

namespace wifi {

  static unsigned long s_begin_ms = 0;     ///< millis() at WiFi.begin() call.
  static bool          s_mdns_started = false;  ///< MDNS.begin() done once per boot.
  static bool          s_ota_started  = false;  ///< ArduinoOTA.begin() done once per boot.

  // --- Boot counter cache (iter 015) ---------------------------------------
  //
  // The counter is incremented in init() and reset by loop() after the box
  // has been running stably for WIFI_BOOT_STABLE_MS. We cache the value in
  // RAM so the loop() check is a single comparison (no NVS read per tick).
  static uint8_t s_boot_count_cached = 0;

  // --- Sticky-bad-BSSID recovery -------------------------------------------
  //
  // ASSOC_EXPIRE (4) / AUTH_EXPIRE (2) mean the AP accepted us briefly then
  // dropped us. We pinned a BSSID that has become sub-optimal (overloaded,
  // hand-off in a mesh, weaker than another AP on the same SSID), or the
  // AP is rate-limiting us. After N such drops in a row without ever
  // reaching GOT_IP we clear the NVS hint and trigger a fresh scan from
  // the main loop. Done in loop() (not the event callback) because
  // WiFi.disconnect() + re-begin() must not run inside the WiFi task.
  static constexpr uint32_t DROP_THRESHOLD = 5;
  static volatile uint32_t  s_consecutive_drops = 0;
  static volatile bool      s_need_rescan = false;

  // --- Rate-limited reconnect (iter 015) -----------------------------------
  //
  // The core's WiFi.setAutoReconnect(true) hammers the AP every ~1 s after
  // every DISCONNECTED event. That triggers anti-bruteforce rate-limiting on
  // some routers (notably Freebox) which then refuses to re-authenticate the
  // client for several minutes -- the box ends up in a hot loop of
  // AUTH_EXPIRE / NO_AP_FOUND events.
  //
  // We disable the core's auto-reconnect and schedule reconnects ourselves
  // with an exponential backoff (3 s, 6 s, 12 s, 24 s, capped at 30 s),
  // reset to the floor on a successful GOT_IP. This gives the AP time to
  // forget the previous association attempt before we try again.
  static constexpr uint32_t RECONNECT_BACKOFF_FLOOR_MS = 3000;
  static constexpr uint32_t RECONNECT_BACKOFF_CAP_MS   = 30000;
  static volatile uint32_t  s_next_retry_ms        = 0;        ///< 0 = no retry scheduled
  static volatile uint32_t  s_retry_backoff_ms     = RECONNECT_BACKOFF_FLOOR_MS;

  static void perform_scan_and_connect(const std::string& ssid, const std::string& pass);
  [[noreturn]] static void start_ap(const char* chip_suffix);

  static const char* disconnect_reason_str(uint8_t r) {
    switch (r) {
      case 2:   return "AUTH_EXPIRE";
      case 3:   return "AUTH_LEAVE";
      case 4:   return "ASSOC_EXPIRE";
      case 5:   return "ASSOC_TOOMANY";
      case 6:   return "NOT_AUTHED";
      case 7:   return "NOT_ASSOCED";
      case 8:   return "ASSOC_LEAVE";
      case 15:  return "4WAY_HANDSHAKE_TIMEOUT";
      case 200: return "BEACON_TIMEOUT";
      case 201: return "NO_AP_FOUND";
      case 202: return "AUTH_FAIL";
      case 203: return "ASSOC_FAIL";
      case 204: return "HANDSHAKE_TIMEOUT";
      case 205: return "CONNECTION_FAIL";     // ESP-IDF v5+
      case 206: return "AP_TSF_RESET";        // ESP-IDF v5+
      case 207: return "ROAMING";             // ESP-IDF v5+
      default:  return "?";
    }
  }

  /// Drop the NVS hint when the AP looks permanently gone.
  static bool reason_invalidates_hint(uint8_t r) {
    return r == 200 /*BEACON_TIMEOUT*/    ||
           r == 201 /*NO_AP_FOUND*/       ||
           r == 203 /*ASSOC_FAIL*/        ||
           r == 205 /*CONNECTION_FAIL*/;
  }

  /// Reasons that should count toward the sticky-bad threshold. An AP that
  /// keeps half-associating then dropping us is the signature of either a
  /// stale BSSID hint or anti-bruteforce rate-limiting -- both are addressed
  /// by a full rescan.
  static bool reason_counts_as_drop(uint8_t r) {
    return r == 2 /*AUTH_EXPIRE*/        ||
           r == 4 /*ASSOC_EXPIRE*/       ||
           r == 205 /*CONNECTION_FAIL*/;
  }

  static void on_event(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
      case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
        s_consecutive_drops = 0;
        // Successful association : drop any pending retry and reset the
        // backoff so the next disconnect starts from the floor again.
        s_next_retry_ms = 0;
        s_retry_backoff_ms = RECONNECT_BACKOFF_FLOOR_MS;
        const unsigned long t = (s_begin_ms != 0) ? (millis() - s_begin_ms) : 0;
        logger::info("wifi", "connected ip=%s rssi=%d channel=%d in %lums",
                     WiFi.localIP().toString().c_str(),
                     static_cast<int>(WiFi.RSSI()),
                     static_cast<int>(WiFi.channel()),
                     t);

        // Update the NVS hint if we connected to a different BSSID than what
        // was saved (covers fallback path, BSSID drift, SSID change).
        uint8_t cur_bssid[6] = {0};
        const uint8_t* live = WiFi.BSSID();
        if (live != nullptr) std::memcpy(cur_bssid, live, 6);
        const uint8_t cur_channel = static_cast<uint8_t>(WiFi.channel());
        const std::string current_ssid = nvs_store::get_wifi_ssid();
        nvs_store::WifiHint saved;
        const bool have_hint = nvs_store::get_wifi_hint(saved);
        const bool needs_update =
            !have_hint ||
            saved.ssid != current_ssid ||
            std::memcmp(saved.bssid, cur_bssid, 6) != 0 ||
            saved.channel != cur_channel;
        if (needs_update) {
          nvs_store::WifiHint hint;
          hint.ssid    = current_ssid;
          std::memcpy(hint.bssid, cur_bssid, 6);
          hint.channel = cur_channel;
          nvs_store::set_wifi_hint(hint);
        }

        // Start mDNS responder so the bridge is reachable as
        // `<hostname>.local` (e.g. somfyrts2mqtt-AB12CD.local). Idempotent
        // per boot : a reconnect to a new BSSID does not need a re-begin.
        if (!s_mdns_started) {
          const char* hn = WiFi.getHostname();
          if (hn != nullptr && *hn != '\0' && MDNS.begin(hn)) {
            MDNS.addService("http", "tcp", 80);
            s_mdns_started = true;
            logger::info("wifi", "mDNS up : %s.local (http :80)", hn);
          } else {
            logger::warn("wifi", "mDNS begin failed for hostname=%s",
                         hn ? hn : "(null)");
          }
        }

        // Start ArduinoOTA so PlatformIO can push firmware via espota
        // (iter 016, dev workflow). End users update via WebOTA. The
        // password protects against arbitrary OTA pushes from other
        // peers on the LAN.
        if (!s_ota_started) {
          const char* hn = WiFi.getHostname();
          if (hn != nullptr && *hn != '\0') {
            ArduinoOTA.setHostname(hn);
            ArduinoOTA.setPassword(OTA_PASSWORD);
            ArduinoOTA.onStart([]() {
              logger::warn("ota", "espota start type=%s",
                           ArduinoOTA.getCommand() == U_FLASH ? "flash" : "fs");
            });
            ArduinoOTA.onEnd([]() {
              logger::info("ota", "espota done, rebooting");
            });
            ArduinoOTA.onError([](ota_error_t e) {
              logger::err("ota", "espota error=%u", static_cast<unsigned>(e));
            });
            ArduinoOTA.begin();
            s_ota_started = true;
            logger::info("ota", "ArduinoOTA ready on %s.local:3232", hn);
          }
        }
        break;
      }
      case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        const uint8_t r = info.wifi_sta_disconnected.reason;
        logger::warn("wifi", "disconnected reason=%u (%s)",
                     static_cast<unsigned>(r), disconnect_reason_str(r));
        if (reason_invalidates_hint(r)) {
          // Next boot will rescan rather than reuse a stale BSSID.
          nvs_store::clear_wifi_hint();
        }
        // AP keeps half-associating or rejecting us. Count toward sticky-bad
        // recovery (full rescan from loop()).
        if (reason_counts_as_drop(r)) {
          if (++s_consecutive_drops >= DROP_THRESHOLD) {
            s_need_rescan = true;
          }
        }
        // Schedule the next reconnect attempt with exponential backoff. Set
        // only if no retry is already pending -- avoids re-arming on
        // duplicate DISCONNECTED events. The flag s_need_rescan above takes
        // precedence in loop() (full rescan instead of reconnect).
        if (s_next_retry_ms == 0) {
          s_next_retry_ms = millis() + s_retry_backoff_ms;
          const uint32_t doubled = s_retry_backoff_ms * 2;
          s_retry_backoff_ms = (doubled > RECONNECT_BACKOFF_CAP_MS)
                               ? RECONNECT_BACKOFF_CAP_MS
                               : doubled;
        }
        break;
      }
      default:
        break;
    }
  }

  /// Scan visible APs and return the strongest BSSID matching @p target_ssid.
  static bool scan_and_pick_best(const char* target_ssid,
                                 uint8_t out_bssid[6],
                                 uint8_t& out_channel,
                                 int32_t& out_rssi) {
    logger::info("wifi", "scanning 2.4 GHz...");
    const int16_t n = WiFi.scanNetworks(/*async*/ false, /*show_hidden*/ true);
    if (n <= 0) {
      logger::warn("wifi", "no AP found (n=%d)", static_cast<int>(n));
      return false;
    }

    int8_t  best_idx  = -1;
    int32_t best_rssi = -127;
    for (int16_t i = 0; i < n; ++i) {
      if (WiFi.SSID(i) == target_ssid && WiFi.RSSI(i) > best_rssi) {
        best_idx  = static_cast<int8_t>(i);
        best_rssi = WiFi.RSSI(i);
      }
    }

    if (best_idx < 0) {
      logger::warn("wifi", "target SSID '%s' not visible (%d APs scanned)",
                   target_ssid, static_cast<int>(n));
      WiFi.scanDelete();
      return false;
    }

    std::memcpy(out_bssid, WiFi.BSSID(best_idx), 6);
    out_channel = static_cast<uint8_t>(WiFi.channel(best_idx));
    out_rssi    = best_rssi;
    WiFi.scanDelete();
    return true;
  }

  void init() {
    // --- Hardening (applies to both AP and STA paths) ---
    //   persistent(false)         : avoid flash wear from auto WiFi config writes
    //   setSleep(false)           : ~50 mA more, ~100 ms less RX latency
    //   setAutoReconnect(false)   : iter 015 -- the core's auto-reconnect retries
    //                               every ~1 s after every DISCONNECTED event,
    //                               which triggers anti-bruteforce on some
    //                               routers. We schedule retries ourselves with
    //                               an exponential backoff (see loop()).
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(false);

    // --- Hostname + chipId suffix (only the suffix is reused for the AP SSID
    //     so two bridges in setup mode don't collide). The STA/mDNS hostname
    //     stays short ("somfyrts2mqtt") -- a future iter can make it
    //     configurable via the web UI if multi-bridge LANs need distinction.
    char chip_suffix[7];
    const char* hostname = "somfyrts2mqtt";
    const uint64_t mac = ESP.getEfuseMac();
    std::snprintf(chip_suffix, sizeof(chip_suffix), "%02X%02X%02X",
                  static_cast<unsigned>((mac >> 16) & 0xFF),
                  static_cast<unsigned>((mac >> 8) & 0xFF),
                  static_cast<unsigned>(mac & 0xFF));
    WiFi.setHostname(hostname);

    // --- Boot counter (4-power-cycles AP recovery) ---
    const uint8_t prev_count = nvs_store::get_boot_count();
    const uint8_t new_count  = wifi_boot::on_boot(prev_count, WIFI_BOOT_AP_THRESHOLD);
    nvs_store::set_boot_count(new_count);
    s_boot_count_cached = new_count;
    const bool force_ap =
        wifi_boot::should_force_ap(new_count, WIFI_BOOT_AP_THRESHOLD);
    if (force_ap) {
      logger::warn("wifi", "boot counter reached %u, forcing AP",
                   static_cast<unsigned>(new_count));
    } else {
      logger::info("wifi", "boot counter %u/%u",
                   static_cast<unsigned>(new_count),
                   static_cast<unsigned>(WIFI_BOOT_AP_THRESHOLD));
    }

    // --- Compile-time creds migration (one-shot, dev / upgrade convenience) ---
    //
    // Two migration cases :
    //  1. Fresh box, NVS empty       : seed both ssid + pass from secrets.h.
    //  2. Upgrade from pre-iter-015  : the old wifi_manager wrote `wifi.ssid`
    //     to NVS as part of the WifiHint, but never wrote `wifi.pass` (the
    //     password lived in compile-time WIFI_PASSWORD only). Without this
    //     branch the upgraded box would read a non-empty ssid + an empty pass
    //     from NVS and fail to associate. Seed the pass when the stored ssid
    //     matches WIFI_SSID and the stored pass is empty.
    std::string nvs_ssid = nvs_store::get_wifi_ssid();
    if (std::strlen(WIFI_SSID) > 0) {
      const std::string compile_ssid = WIFI_SSID;
      const std::string nvs_pass     = nvs_store::get_wifi_pass();
      const bool upgrade_seed_pass   = (nvs_ssid == compile_ssid) && nvs_pass.empty();
      if (nvs_ssid.empty() || upgrade_seed_pass) {
        if (nvs_store::set_wifi_creds(compile_ssid, WIFI_PASSWORD)) {
          logger::info("wifi", "%s compile-time creds to NVS ssid=%s",
                       nvs_ssid.empty() ? "migrated" : "upgrade-seeded",
                       compile_ssid.c_str());
          nvs_ssid = compile_ssid;
        }
      }
    }

    // --- AP branch (BLOCKING, ends with ESP.restart()) ---
    if (force_ap || nvs_ssid.empty()) {
      logger::warn("wifi", "entering AP mode (force=%d, ssid_empty=%d)",
                   static_cast<int>(force_ap), static_cast<int>(nvs_ssid.empty()));
      // Reset counter BEFORE entering AP. If the user reboots out of AP
      // (timeout or save), the next boot starts fresh at 1.
      nvs_store::set_boot_count(0);
      s_boot_count_cached = 0;
      start_ap(chip_suffix);
      // start_ap() always restarts. Defensive infinite loop in case it ever
      // returns (it shouldn't).
      while (true) { delay(1000); }
    }

    // --- STA path ---
    WiFi.mode(WIFI_STA);
    // ESP32-C3 Super Mini PA is miscalibrated above ~15 dBm on some boards :
    // the saturated TX corrupts the WPA2 4-way handshake and the AP times
    // out auth (AUTH_EXPIRE loop). 8.5 dBm is the community-standard safe
    // value (~7 mW, plenty for home WiFi).
    // Refs : arduino-esp32 #6767, Arduino forum #1264358. Ported from main.
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.onEvent(on_event);

    const std::string nvs_pass = nvs_store::get_wifi_pass();

    // Fast path: reuse the NVS hint if it matches our stored SSID.
    nvs_store::WifiHint hint;
    if (nvs_store::get_wifi_hint(hint) && hint.ssid == nvs_ssid) {
      logger::info("wifi",
                   "fast path ssid=%s bssid=%02X:%02X:%02X:%02X:%02X:%02X ch=%u hostname=%s",
                   nvs_ssid.c_str(),
                   hint.bssid[0], hint.bssid[1], hint.bssid[2],
                   hint.bssid[3], hint.bssid[4], hint.bssid[5],
                   hint.channel, hostname);
      s_begin_ms = millis();
      WiFi.begin(nvs_ssid.c_str(), nvs_pass.c_str(), hint.channel, hint.bssid);
      return;
    }

    // Slow path: scan + pick best + save hint + connect.
    perform_scan_and_connect(nvs_ssid, nvs_pass);
  }

  static void perform_scan_and_connect(const std::string& ssid,
                                       const std::string& pass) {
    uint8_t bssid[6] = {0};
    uint8_t channel  = 0;
    int32_t rssi     = -127;
    if (scan_and_pick_best(ssid.c_str(), bssid, channel, rssi)) {
      nvs_store::WifiHint new_hint;
      new_hint.ssid    = ssid;
      std::memcpy(new_hint.bssid, bssid, 6);
      new_hint.channel = channel;
      nvs_store::set_wifi_hint(new_hint);

      logger::info("wifi",
                   "pinning ssid=%s bssid=%02X:%02X:%02X:%02X:%02X:%02X ch=%u rssi=%ld",
                   ssid.c_str(),
                   bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                   channel, static_cast<long>(rssi));
      s_begin_ms = millis();
      WiFi.begin(ssid.c_str(), pass.c_str(), channel, bssid);
      return;
    }

    // Last-resort fallback: let the core figure it out.
    logger::warn("wifi", "fallback to default begin (no hint, no scan match)");
    s_begin_ms = millis();
    WiFi.begin(ssid.c_str(), pass.c_str());
  }

  /// Hand off to the Sowel-styled captive portal (spec 018, replaces tzapu).
  /// BLOCKS until the user submits or the AP timeout fires; the portal owns
  /// the NVS persist + reboot on its own, so this function does not return.
  [[noreturn]] static void start_ap(const char* chip_suffix) {
    char ap_ssid[32];
    std::snprintf(ap_ssid, sizeof(ap_ssid),
                  "%s%s", WIFI_AP_SSID_PREFIX, chip_suffix);

    logger::warn("wifi", "captive portal SSID=%s timeout=%us",
                 ap_ssid, static_cast<unsigned>(WIFI_AP_TIMEOUT_S));

    captive_portal::run(ap_ssid, WIFI_AP_TIMEOUT_S);
  }

  void loop() {
    // --- ArduinoOTA service ticks (iter 016) --------------------------------
    // Drains the espota TCP/UDP socket for any pending update push. Cheap
    // when idle (~couple of µs / call) so we tick it unconditionally rather
    // than gate on WL_CONNECTED.
    if (s_ota_started) {
      ArduinoOTA.handle();
    }

    // --- Boot counter reset after stable uptime AND a stable WiFi association
    //
    // Gating on WL_CONNECTED matters more than the uptime alone : a Super Mini
    // with a marginal LDO can brown out during the high-current transients of
    // WiFi association + MQTT TLS handshake. Doing a flash write
    // (set_boot_count(0)) at the wrong moment was observed to coincide with
    // chip resets. Once WiFi is associated AND we are past WIFI_BOOT_STABLE_MS,
    // the current draw is back to a stable baseline and the write is safe.
    if (s_boot_count_cached != 0 &&
        WiFi.status() == WL_CONNECTED &&
        wifi_boot::should_reset(s_boot_count_cached, millis(), WIFI_BOOT_STABLE_MS)) {
      nvs_store::set_boot_count(0);
      s_boot_count_cached = 0;
      logger::info("wifi", "boot counter reset (stable uptime reached)");
    }

    // --- Sticky-bad-BSSID recovery (takes precedence over a pending retry) --
    if (s_need_rescan) {
      // Reset BEFORE the disconnect+begin to avoid re-arming on the
      // synthetic DISCONNECTED event we are about to generate.
      s_need_rescan = false;
      s_consecutive_drops = 0;
      s_next_retry_ms = 0;  // rescan supersedes any pending reconnect
      logger::warn("wifi",
                   "sticky-bad BSSID after %u drops, clearing hint and rescanning",
                   static_cast<unsigned>(DROP_THRESHOLD));
      nvs_store::clear_wifi_hint();
      // disconnect(false, false) : stop STA, keep radio on, keep config.
      WiFi.disconnect(false, false);
      delay(100);
      const std::string ssid = nvs_store::get_wifi_ssid();
      const std::string pass = nvs_store::get_wifi_pass();
      perform_scan_and_connect(ssid, pass);
      return;
    }

    // --- Rate-limited reconnect (iter 015) ----------------------------------
    //
    // Auto-reconnect is disabled at init() ; we drive reconnects from here
    // with an exponential backoff scheduled by the DISCONNECTED handler.
    // The check is cheap (two volatile reads + a compare) so it can run
    // every tick.
    if (s_next_retry_ms != 0 &&
        WiFi.status() != WL_CONNECTED &&
        millis() >= s_next_retry_ms) {
      const uint32_t backoff = s_retry_backoff_ms;  // capture for log
      s_next_retry_ms = 0;
      logger::info("wifi", "reconnect attempt (next backoff %ums)",
                   static_cast<unsigned>(backoff));
      s_begin_ms = millis();
      // WiFi.reconnect() reuses the last begin() args (ssid + pass + hint
      // if any). Avoids re-reading NVS in the hot path.
      WiFi.reconnect();
    }
  }

  bool is_connected() {
    return WiFi.status() == WL_CONNECTED;
  }

}
