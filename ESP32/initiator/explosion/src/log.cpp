#include "log.h"

#include <Arduino.h>

#include "config.h"

namespace {

LogRecord buffer[LogBufferSize];
uint8_t head = 0;  // next slot to write
uint8_t count = 0; // records waiting
uint16_t dropped = 0;

// A spinlock rather than a mutex: every critical section here is a handful of
// stores, and logWrite is called from four tasks including the radio task,
// where blocking on a mutex held by a lower-priority task would delay a frame.
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

#if LOG_TO_UART
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
#endif

} // namespace

void logInit() {
  portENTER_CRITICAL(&lock);
  head = 0;
  count = 0;
  dropped = 0;
  portEXIT_CRITICAL(&lock);
}

void logWrite(uint8_t level, uint8_t tag, uint8_t code, int32_t arg) {
  const uint32_t now = millis();

  portENTER_CRITICAL(&lock);

  buffer[head].timestampMs = now;
  buffer[head].level = level;
  buffer[head].tag = tag;
  buffer[head].code = code;
  buffer[head].arg = arg;

  head = (uint8_t)((head + 1) % LogBufferSize);

  if (count < LogBufferSize) {
    count++;
  } else {
    // Full: this write has just overwritten the oldest unsent record. Counted
    // rather than silently absorbed, because "we lost some" and "nothing
    // happened" look identical otherwise.
    dropped++;
  }

  portEXIT_CRITICAL(&lock);

#if LOG_TO_UART
  Serial.printf("[%8lu] %-5s t%u c%u %ld\n", (unsigned long)now, levelName(level),
                (unsigned)tag, (unsigned)code, (long)arg);
#endif
}

uint8_t logTake(LogRecord *out, uint8_t max) {
  if (out == nullptr || max == 0) return 0;

  portENTER_CRITICAL(&lock);

  const uint8_t taken = count < max ? count : max;
  // head - count is the oldest record, modulo the ring.
  uint8_t index = (uint8_t)((head + LogBufferSize - count) % LogBufferSize);

  for (uint8_t i = 0; i < taken; i++) {
    out[i] = buffer[index];
    index = (uint8_t)((index + 1) % LogBufferSize);
  }

  count = (uint8_t)(count - taken);

  portEXIT_CRITICAL(&lock);

  return taken;
}

uint8_t logPending() {
  portENTER_CRITICAL(&lock);
  const uint8_t pending = count;
  portEXIT_CRITICAL(&lock);
  return pending;
}

uint16_t logDropped() {
  portENTER_CRITICAL(&lock);
  const uint16_t lost = dropped;
  portEXIT_CRITICAL(&lock);
  return lost;
}
