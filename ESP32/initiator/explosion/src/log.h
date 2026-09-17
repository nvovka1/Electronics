#pragma once

#include <stdint.h>

// A ring log of codes, not sentences.
//
// The same fact as formatted text costs about five times the bytes and cannot
// be filtered without a regular expression. The words are put back by the
// backend, from Initiator.Domain/Logs/LogDictionary.cs.
//
// THESE NUMBERS ARE A CONTRACT with that file. Change one here without changing
// it there - or the other way round - and every stored record decodes to the
// wrong story, silently, which is the worst way for it to happen. Codes are
// grouped in tens with gaps so a new one can go next to its relatives;
// renumbering an existing one is never correct, because records already stored
// carry the old number.

enum LogLevel : uint8_t {
  LevelPanic = 0,
  LevelError = 1,
  LevelWarn = 2,
  LevelInfo = 3,
  LevelDebug = 4,
  LevelTrace = 5,
};

enum LogTag : uint8_t {
  TagSys = 0,
  TagPost = 1,
  TagCfg = 2,
  TagRadio = 3,
  TagState = 4,
  TagLed = 5,
  TagUi = 6,
  TagNet = 7,
  TagBatt = 8,
};

enum LogCode : uint8_t {
  CodeNone = 0,

  CodeBoot = 1,
  CodeBootCount = 2,
  CodeFwDirty = 3,
  CodeTaskStartFail = 4,
  CodeWdtSubscribed = 5,

  CodePostPass = 10,
  CodePostFail = 11,
  CodePostMask = 12,

  CodeCfgLoaded = 20,
  CodeCfgDefaults = 21,
  CodeCfgSaved = 22,
  CodeCfgSaveFail = 23,
  CodeCfgChanged = 24,

  CodeRadioReady = 30,
  CodeRadioInitFail = 31,
  CodeCmdRx = 32,
  CodeCmdNotForUs = 33,
  CodeCmdReplay = 34,
  CodeRxBadFrame = 35,
  CodeAckTx = 36,
  CodeAnnounceTx = 37,
  CodeCmdTx = 38,
  CodeAckRx = 39,
  CodeTxNoAck = 40,

  CodeStateEnter = 50,
  CodeCmdAccepted = 51,
  CodeCmdRejected = 52,
  CodeCountdownStarted = 53,
  CodeCountdownRestarted = 54,
  CodeCountdownCancelled = 55,
  CodeAutoArm = 56,

  CodeWifiUp = 70,
  CodeWifiDown = 71,
  CodeReportOk = 72,
  CodeReportFail = 73,
  CodeCmdPolled = 74,
  CodeEventPosted = 75,
  CodeEventBuffered = 76,
  CodeLogSent = 77,

  CodeBattLow = 90,
  CodeBattUntrusted = 91,
};

struct LogRecord {
  uint32_t timestampMs; // this node's monotonic clock; never wall-clock time
  uint8_t level;
  uint8_t tag;
  uint8_t code;
  int32_t arg;
};

void logInit();

// Safe from any task. Records above LOG_COMPILE_LEVEL are not merely filtered,
// they are not compiled - so they cannot switch themselves back on in the
// field.
void logWrite(uint8_t level, uint8_t tag, uint8_t code, int32_t arg);

// Two values in one record, because two records can be split by a ring wrap and
// then only half the story survives. The backend unpacks these.
static inline int32_t logPack(uint8_t low, uint8_t high) {
  return (int32_t)((uint32_t)low | ((uint32_t)high << 8));
}

// Oldest first. Returns how many were written into `out`.
uint8_t logTake(LogRecord *out, uint8_t max);

// How many records are waiting to be uploaded.
uint8_t logPending();

// Records lost to the ring wrapping before they could be sent. Worth reporting:
// it is the difference between "nothing happened" and "we did not see it".
uint16_t logDropped();

#ifndef LOG_COMPILE_LEVEL
#define LOG_COMPILE_LEVEL 5
#endif

#define LOG_AT(level, tag, code, arg)                                          \
  do {                                                                         \
    if ((level) <= LOG_COMPILE_LEVEL) logWrite((level), (tag), (code), (arg)); \
  } while (0)

#define LOG_ERROR(tag, code, arg) LOG_AT(LevelError, tag, code, arg)
#define LOG_WARN(tag, code, arg) LOG_AT(LevelWarn, tag, code, arg)
#define LOG_INFO(tag, code, arg) LOG_AT(LevelInfo, tag, code, arg)
#define LOG_DEBUG(tag, code, arg) LOG_AT(LevelDebug, tag, code, arg)
