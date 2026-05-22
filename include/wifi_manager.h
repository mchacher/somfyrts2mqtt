/**
 * @file wifi_manager.h
 * @brief WiFi commissioning + non-blocking STA with sticky-bad-BSSID recovery.
 */
#pragma once

/**
 * @namespace wifi
 * @brief Boot-time WiFi commissioning, then non-blocking STA with reconnect.
 *
 * Credentials live in NVS (`wifi.ssid` / `wifi.pass`, see nvs_store).
 * `include/secrets.h` is only a one-shot bootstrap: if the constants
 * `WIFI_SSID` / `WIFI_PASSWORD` are non-empty at compile time AND NVS
 * has no creds yet, they get migrated to NVS on first boot.
 *
 * Boot decision tree (iter 015) :
 * - 4 rapid power-cycles (tracked by a saturating NVS counter, reset
 *   after WIFI_BOOT_STABLE_MS of stable uptime) → AP captive portal.
 * - No creds in NVS → AP captive portal.
 * - Otherwise → STA with the existing scan + BSSID-pinning fast path.
 *
 * In AP mode, `init()` is BLOCKING : it runs the tzapu/WiFiManager
 * captive portal until the user saves new creds or the AP timeout
 * fires (`WIFI_AP_TIMEOUT_S`), then `ESP.restart()`. The rest of the
 * firmware (admin UI, MQTT, orchestrator) never initialises in AP
 * mode -- the box reboots into the STA path with the freshly saved
 * creds.
 *
 * In STA mode, `init()` is non-blocking and returns immediately. The
 * ESP32 core handles auto-reconnect on drop ; `loop()` watches for
 * sticky-bad-BSSID patterns and triggers a rescan + reconnect when
 * the pinned BSSID drops us repeatedly.
 */
namespace wifi {

  /**
   * @brief Run the boot decision and start the appropriate mode.
   *
   * Increments the boot counter (4-power-cycles recovery), decides
   * between AP and STA based on the counter + NVS credentials, and
   * either blocks in the captive portal until save+reboot or returns
   * immediately with `WiFi.begin()` in flight.
   */
  void init();

  /**
   * @brief Per-tick hook. Resets the boot counter after stable uptime
   * and drives the sticky-bad-BSSID recovery.
   */
  void loop();

  /**
   * @brief Whether the station is currently associated and has an IP.
   * @return true if WiFi.status() == WL_CONNECTED.
   */
  bool is_connected();

}
