#include "log.h"

#include <Arduino.h>
#include <stdarg.h>

static const size_t LineMax = 96;
static const size_t LineCount = 24;

static char _lines[LineCount][LineMax];
static size_t _next = 0;
static size_t _stored = 0;
static SemaphoreHandle_t _mutex = nullptr;

void logInit() {
  _mutex = xSemaphoreCreateMutex();
  _next = 0;
  _stored = 0;
}

static const char *levelName(LogLevel level) {
  switch (level) {
    case LevelError:
      return "ERR";
    case LevelWarn:
      return "WRN";
    case LevelInfo:
      return "INF";
    default:
      return "DBG";
  }
}

void logPrintf(LogLevel level, const char *tag, const char *format, ...) {
  char message[LineMax];

  va_list arguments;
  va_start(arguments, format);
  vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);

  char line[LineMax];
  snprintf(line, sizeof(line), "%8lu %s %-4s %s", (unsigned long)millis(), levelName(level), tag,
           message);

  Serial.println(line);

  // The ring is a convenience for the web page, so a failure to take the lock
  // drops the line rather than delaying whoever was logging. Nothing depends on
  // this buffer being complete.
  if (_mutex != nullptr && xSemaphoreTake(_mutex, 0) == pdTRUE) {
    strncpy(_lines[_next], line, LineMax - 1);
    _lines[_next][LineMax - 1] = 0;
    _next = (_next + 1) % LineCount;
    if (_stored < LineCount) _stored++;
    xSemaphoreGive(_mutex);
  }
}

size_t logRecent(char *out, size_t max) {
  if (out == nullptr || max == 0) return 0;
  out[0] = 0;
  if (_mutex == nullptr) return 0;
  if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;

  size_t length = 0;
  const size_t first = (_stored == LineCount) ? _next : 0;

  for (size_t i = 0; i < _stored; i++) {
    const char *line = _lines[(first + i) % LineCount];
    const size_t lineLength = strlen(line);
    if (length + lineLength + 2 > max) break;
    memcpy(out + length, line, lineLength);
    length += lineLength;
    out[length++] = '\n';
    out[length] = 0;
  }

  xSemaphoreGive(_mutex);
  return length;
}
