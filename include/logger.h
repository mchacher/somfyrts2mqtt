#pragma once

namespace logger {
  void info(const char* tag, const char* fmt, ...);
  void warn(const char* tag, const char* fmt, ...);
  void err (const char* tag, const char* fmt, ...);
}
