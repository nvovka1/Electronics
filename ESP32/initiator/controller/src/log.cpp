#include "log.h"

#include <Arduino.h>

namespace {

const char *levelName(uint8_t level) {
  switch (level) {
  case LevelPanic: return "PANIC";
  case LevelError: return "ERROR";
  case LevelWarn:  return "WARN";
  case LevelInfo:  return "INFO";
  case LevelDebug: return "DEBUG";
  default:         return "TRACE";
  }
}

} // namespace

void logInit() {}

void logWrite(uint8_t level, uint8_t tag, uint8_t code, int32_t arg) {
#if LOG_TO_UART
  Serial.printf("[%8lu] %-5s t%u c%u %ld\n", (unsigned long)millis(), levelName(level),
                (unsigned)tag, (unsigned)code, (long)arg);
#else
  // A field build with no UART logging has nowhere to put these. The arguments
  // are still referenced so the compiler does not warn about every call site.
  (void)level;
  (void)tag;
  (void)code;
  (void)arg;
#endif
}
