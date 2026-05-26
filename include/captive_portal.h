/**
 * @file captive_portal.h
 * @brief First-run captive portal for WiFi credentials.
 *
 * Self-contained AP + DNSServer + AsyncWebServer captive portal modeled
 * on sowel-energy-display/src/portal.cpp (spec 018). Replaces the tzapu
 * WiFiManager that used to live inline in wifi_manager.cpp, so the
 * first-run experience visually matches the rest of the Sowel device
 * family (same brand header, light palette, system fonts, eye-toggle
 * password widget, same DOM scaffold).
 */
#pragma once

#include <cstdint>

namespace captive_portal {

  /**
   * @brief Run the captive portal until the user submits WiFi credentials
   *        or the idle timeout expires. BLOCKS the caller.
   *
   * Behaviour mirrors what wifi_manager::start_ap() did under tzapu:
   *   1. Bring up an AP on the given SSID at 192.168.4.1 (AP_STA mode so
   *      WiFi.scanNetworks() works during portal lifetime).
   *   2. Clamp TX power to WIFI_POWER_8_5dBm (C3 Super Mini PA-saturation
   *      workaround, see CLAUDE.md).
   *   3. Start a DNSServer catch-all so phones detect the captive page.
   *   4. Serve a single Sowel-styled HTML form on /, with iOS / Android
   *      / Windows captive-probe URLs all redirecting to /.
   *   5. On POST /save: validate, persist via nvs_store::set_wifi_creds(),
   *      schedule a deferred ESP.restart() (deferred because the AsyncTCP
   *      callback cannot safely restart synchronously — see portal.cpp
   *      for the same explanation).
   *   6. Idle timeout → ESP.restart() to retry STA path on next boot.
   *
   * Always exits via ESP.restart(); never returns.
   *
   * @param ap_ssid     null-terminated AP SSID (e.g. "somfyrts2mqtt-A1B2")
   * @param timeout_s   idle timeout in seconds (no form submit → reboot)
   */
  [[noreturn]] void run(const char* ap_ssid, uint32_t timeout_s);

}
