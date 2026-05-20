/**
 * @file logger.cpp
 * @brief Logger implementation. See logger.h for the API.
 */
#include "logger.h"
#include <Arduino.h>
#include <cstdarg>

namespace logger {

  static constexpr size_t LOG_BUF_SIZE = 256;

  static void vlog(const char* tag, const char* fmt, va_list args) {
    char buf[LOG_BUF_SIZE];
    vsnprintf(buf, sizeof(buf), fmt, args);
    Serial.printf("[%s] %s\n", tag, buf);
  }

  void info(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(tag, fmt, args);
    va_end(args);
  }

  void warn(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(tag, fmt, args);
    va_end(args);
  }

  void err(const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlog(tag, fmt, args);
    va_end(args);
  }

}
