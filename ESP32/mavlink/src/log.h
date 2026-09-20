#pragma once

#include <stddef.h>
#include <stdint.h>

// Plain text, not the coded ring the initiator nodes use.
//
// Worth saying why they differ. Over there the codes exist because the log is
// uploaded and reconstructed by the backend from a dictionary that ships with
// the firmware, so the numbers are a contract. Nothing uploads this log: it is
// read by a person, on the serial monitor or on the board's own web page, while
// they work out why a flight is not being recorded. Text is the right shape for
// that, and a dictionary would be a contract with nobody.

enum LogLevel : uint8_t {
  LevelError = 1,
  LevelWarn = 2,
  LevelInfo = 3,
  LevelDebug = 4,
};

void logInit();

void logPrintf(LogLevel level, const char *tag, const char *format, ...);

// The last few lines, oldest first, for the board's web page. The page is the
// only diagnosis available once the USB cable is out, which is most of the time
// this firmware is doing anything interesting.
size_t logRecent(char *out, size_t max);

#define LOG_ERROR(tag, ...) logPrintf(LevelError, tag, __VA_ARGS__)
#define LOG_WARN(tag, ...) logPrintf(LevelWarn, tag, __VA_ARGS__)
#define LOG_INFO(tag, ...) logPrintf(LevelInfo, tag, __VA_ARGS__)
#define LOG_DEBUG(tag, ...) logPrintf(LevelDebug, tag, __VA_ARGS__)
