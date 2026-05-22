/**
 * @file wifi_manager.cpp
 * @brief WiFi manager: scan + pin best BSSID, Tasmota-style. See wifi_manager.h.
 */
#include "wifi_manager.h"

#include <Arduino.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>

#include "logger.h"
#include "nvs_store.h"
#include "secrets.h"

namespace wifi {

  static unsigned long s_begin_ms = 0;     ///< millis() at WiFi.begin() call.

  // --- Sticky-bad-BSSID recovery ------------------------------------------
  //
  // ASSOC_EXPIRE (4) / AUTH_EXPIRE (2) mean the AP accepted us briefly then
  // dropped us. The core's auto-reconnect keeps retrying the SAME BSSID
  // forever, which loops if the pinned BSSID has become sub-optimal
  // (overloaded, hand-off in a mesh, weaker than another AP on the same
  // SSID). When we see N such drops in a row without ever reaching GOT_IP,
  // we clear the NVS hint and trigger a fresh scan from the main loop.
  // Done in loop() (not the event callback) because WiFi.disconnect() +
  // re-begin() must not run inside the WiFi task context.
  static constexpr uint32_t DROP_THRESHOLD = 5;
  static volatile uint32_t  s_consecutive_drops = 0;
  static volatile bool      s_need_rescan = false;

  static void perform_scan_and_connect();

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
        nvs_store::WifiHint saved;
        const bool have_hint = nvs_store::get_wifi_hint(saved);
        const bool needs_update =
            !have_hint ||
            saved.ssid != WIFI_SSID ||
            std::memcmp(saved.bssid, cur_bssid, 6) != 0 ||
            saved.channel != cur_channel;
        if (needs_update) {
          nvs_store::WifiHint hint;
          hint.ssid    = WIFI_SSID;
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
    // --- Hardening ---
    //   persistent(false) : avoid flash wear from automatic WiFi config writes
    //   setSleep(false)   : ~50 mA more, ~100 ms less RX latency (we're USB-powered)
    //   setAutoReconnect  : the core handles drops on the same BSSID
    //   setTxPower(8.5dBm): ESP32-C3 Super Mini PA is miscalibrated above
    //     ~15 dBm on some boards : the saturated TX corrupts the WPA2
    //     4-way handshake and the AP times out auth (AUTH_EXPIRE loop).
    //     8.5 dBm is the community-standard conservative value (Arduino
    //     forum, ESPHome, GitHub) -- safe under every observed threshold,
    //     ~7 mW output, plenty for home WiFi range. Harmless on WROOM.
    //     Refs : arduino-esp32 #6767, Arduino forum #1264358.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    char hostname[24];
    const uint64_t mac = ESP.getEfuseMac();
    std::snprintf(hostname, sizeof(hostname), "somfyrts2mqtt-%02X%02X%02X",
                  static_cast<unsigned>((mac >> 16) & 0xFF),
                  static_cast<unsigned>((mac >> 8) & 0xFF),
                  static_cast<unsigned>(mac & 0xFF));
    WiFi.setHostname(hostname);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.onEvent(on_event);

    // --- Fast path: reuse the NVS hint if it matches our SSID ---
    nvs_store::WifiHint hint;
    if (nvs_store::get_wifi_hint(hint) && hint.ssid == WIFI_SSID) {
      logger::info("wifi",
                   "fast path bssid=%02X:%02X:%02X:%02X:%02X:%02X ch=%u hostname=%s",
                   hint.bssid[0], hint.bssid[1], hint.bssid[2],
                   hint.bssid[3], hint.bssid[4], hint.bssid[5],
                   hint.channel, hostname);
      s_begin_ms = millis();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD, hint.channel, hint.bssid);
      return;
    }

    // --- Slow path: scan + pick best + save hint + connect ---
    perform_scan_and_connect();
  }

  static void perform_scan_and_connect() {
    uint8_t bssid[6] = {0};
    uint8_t channel  = 0;
    int32_t rssi     = -127;
    if (scan_and_pick_best(WIFI_SSID, bssid, channel, rssi)) {
      nvs_store::WifiHint new_hint;
      new_hint.ssid    = WIFI_SSID;
      std::memcpy(new_hint.bssid, bssid, 6);
      new_hint.channel = channel;
      nvs_store::set_wifi_hint(new_hint);

      logger::info("wifi",
                   "pinning bssid=%02X:%02X:%02X:%02X:%02X:%02X ch=%u rssi=%ld",
                   bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5],
                   channel, static_cast<long>(rssi));
      s_begin_ms = millis();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD, channel, bssid);
      return;
    }

    // --- Last-resort fallback: let the core figure it out ---
    logger::warn("wifi", "fallback to default begin (no hint, no scan match)");
    s_begin_ms = millis();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  void loop() {
    if (s_need_rescan) {
      // Reset BEFORE the disconnect+begin to avoid re-arming on the
      // synthetic DISCONNECTED event we are about to generate.
      s_need_rescan = false;
      s_consecutive_drops = 0;
      logger::warn("wifi",
                   "sticky-bad BSSID after %u drops, clearing hint and rescanning",
                   static_cast<unsigned>(DROP_THRESHOLD));
      nvs_store::clear_wifi_hint();
      // disconnect(false, false) : stop STA, keep radio on, keep config.
      WiFi.disconnect(false, false);
      delay(100);
      perform_scan_and_connect();
    }
  }

  bool is_connected() {
    return WiFi.status() == WL_CONNECTED;
  }

}
