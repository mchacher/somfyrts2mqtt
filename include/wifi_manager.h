/**
 * @file wifi_manager.h
 * @brief Non-blocking WiFi station with automatic reconnect.
 */
#pragma once

/**
 * @namespace wifi
 * @brief Non-blocking WiFi station with automatic reconnect.
 *
 * Credentials come from include/secrets.h (gitignored). State
 * transitions are reported through the ESP32 WiFi event callback,
 * which logs via the logger module. The ESP32 core handles the
 * auto-reconnect via WiFi.setAutoReconnect(true).
 */
namespace wifi {

  /**
   * @brief Initialise WiFi station mode and start connecting.
   *
   * Registers the WiFi event handler and triggers WiFi.begin() with
   * the credentials defined in secrets.h. Returns immediately;
   * the connection completes asynchronously and is logged via events.
   */
  void init();

  /**
   * @brief Per-tick hook. Currently a no-op.
   *
   * Kept as a convention so the rest of the codebase can call
   * wifi::loop() without checking whether it exists. Reserved for
   * future watchdog or status-reporting logic.
   */
  void loop();

  /**
   * @brief Whether the station is currently associated and has an IP.
   * @return true if WiFi.status() == WL_CONNECTED.
   */
  bool is_connected();

}
