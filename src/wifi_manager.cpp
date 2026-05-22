/**
 * @file wifi_manager.cpp
 * @brief WiFi manager: boot decision tree + captive portal + scan/pin BSSID.
 *        See wifi_manager.h and specs/015-wifi-captive-portal/.
 */
#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>   // tzapu/WiFiManager (MIT)
#include <cstdio>
#include <cstring>

#include "config.h"
#include "logger.h"
#include "nvs_store.h"
#include "secrets.h"
#include "wifi_boot.h"

namespace wifi {

  static unsigned long s_begin_ms = 0;     ///< millis() at WiFi.begin() call.

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
  static void start_ap(const char* chip_suffix);

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
      default:  return "?";
    }
  }

  /// Drop the NVS hint when the AP looks permanently gone.
  static bool reason_invalidates_hint(uint8_t r) {
    return r == 200 /*BEACON_TIMEOUT*/ ||
           r == 201 /*NO_AP_FOUND*/    ||
           r == 203 /*ASSOC_FAIL*/;
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
        // ASSOC_EXPIRE / AUTH_EXPIRE = AP accepted us then dropped us. If
        // the pinned BSSID keeps doing this, our hint is sticky-bad : ask
        // loop() to clear it + rescan.
        if (r == 4 /*ASSOC_EXPIRE*/ || r == 2 /*AUTH_EXPIRE*/) {
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

    // --- Hostname (6-hex chipId suffix, reused for the AP SSID) ---
    char chip_suffix[7];
    char hostname[24];
    const uint64_t mac = ESP.getEfuseMac();
    std::snprintf(chip_suffix, sizeof(chip_suffix), "%02X%02X%02X",
                  static_cast<unsigned>((mac >> 16) & 0xFF),
                  static_cast<unsigned>((mac >> 8) & 0xFF),
                  static_cast<unsigned>(mac & 0xFF));
    std::snprintf(hostname, sizeof(hostname), "somfyrts2mqtt-%s", chip_suffix);
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

  /// Run the tzapu captive portal (BLOCKS until save or AP timeout), persist
  /// any saved creds to NVS, then `ESP.restart()`.
  static void start_ap(const char* chip_suffix) {
    char ap_ssid[32];
    std::snprintf(ap_ssid, sizeof(ap_ssid),
                  "%s%s", WIFI_AP_SSID_PREFIX, chip_suffix);

    WiFiManager wm;
    // setBreakAfterConfig(true) makes startConfigPortal return as soon as the
    // user saves, instead of having tzapu try to WiFi.begin() with the entered
    // creds itself. We want to control the persist + restart ourselves.
    wm.setBreakAfterConfig(true);
    wm.setConfigPortalTimeout(WIFI_AP_TIMEOUT_S);
    wm.setDebugOutput(false);  // tzapu's debug stream is very verbose

    logger::warn("wifi", "captive portal SSID=%s timeout=%us",
                 ap_ssid, static_cast<unsigned>(WIFI_AP_TIMEOUT_S));

    const bool saved = wm.startConfigPortal(ap_ssid);

    if (saved) {
      const String entered_ssid = wm.getWiFiSSID();
      const String entered_pass = wm.getWiFiPass();
      if (entered_ssid.length() > 0) {
        const std::string s = entered_ssid.c_str();
        const std::string p = entered_pass.c_str();
        if (nvs_store::set_wifi_creds(s, p)) {
          logger::info("wifi", "saved new creds ssid=%s, rebooting into STA",
                       s.c_str());
        } else {
          logger::err("wifi", "failed to persist creds to NVS, rebooting anyway");
        }
      } else {
        logger::warn("wifi", "captive portal returned saved=true but SSID empty");
      }
    } else {
      logger::warn("wifi",
                   "captive portal timed out after %us without save, rebooting",
                   static_cast<unsigned>(WIFI_AP_TIMEOUT_S));
    }

    delay(500);    // let the log flush over Serial / network before reboot
    ESP.restart();
  }

  void loop() {
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
