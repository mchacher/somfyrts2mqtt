/**
 * @file logger.h
 * @brief Lightweight serial logging with [tag] prefix.
 */
#pragma once

/**
 * @namespace logger
 * @brief Lightweight serial logging with [tag] prefix.
 *
 * Wraps Serial.printf. No level filtering yet — info/warn/err share
 * the same output format. The function choice documents intent and
 * reserves the API for future level filtering.
 */
namespace logger {

  /**
   * @brief Emit an info-level log line.
   * @param tag  Module tag prefixed in brackets (e.g. "wifi", "boot").
   * @param fmt  printf-style format string.
   * @param ...  printf-style arguments.
   */
  void info(const char* tag, const char* fmt, ...);

  /**
   * @brief Emit a warning-level log line.
   * @param tag  Module tag prefixed in brackets.
   * @param fmt  printf-style format string.
   * @param ...  printf-style arguments.
   */
  void warn(const char* tag, const char* fmt, ...);

  /**
   * @brief Emit an error-level log line.
   * @param tag  Module tag prefixed in brackets.
   * @param fmt  printf-style format string.
   * @param ...  printf-style arguments.
   */
  void err(const char* tag, const char* fmt, ...);

}
