/**
 * @file web_ui.h
 * @brief Local LAN web UI for bridge configuration.
 */
#pragma once

/**
 * @namespace web_ui
 * @brief HTTP server exposing a single-page UI + JSON REST API.
 *
 * Serves a small HTML page that lets the user edit the MQTT broker
 * config, list / add / delete virtual remotes, and trigger a factory
 * reset. STA mode only; the server binds the IP assigned by the
 * router. No authentication (LAN-only).
 */
namespace web_ui {

  /**
   * @brief Start the AsyncWebServer on port 80.
   *
   * Idempotent: calling twice is a no-op. Must be called after
   * `wifi::init()`. The server runs on its own task; nothing to do
   * in the main `loop()`.
   */
  void init();

}
