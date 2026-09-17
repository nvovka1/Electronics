#pragma once

#include <stdint.h>

// Logging for the controller.
//
// Simpler than the node's on purpose: this board has no network, so there is
// nobody to upload a ring log to and nothing to reconstruct codes into
// sentences. Records go straight to the serial line and nowhere else, which
// means the ring buffer, the dictionary contract and the upload path all
// disappear.
//
// The codes are kept numerically in step with the node's anyway, so the two
// firmwares can be read side by side without a translation in your head.

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
  TagCfg = 2,
  TagRadio = 3,
  TagButton = 5,
  TagUi = 6,
};

enum LogCode : uint8_t {
  CodeBoot = 1,
  CodeBootCount = 2,
  CodeTaskStartFail = 4,

  CodeCfgLoaded = 20,
  CodeCfgDefaults = 21,
  CodeCfgSaved = 22,
  CodeCfgSaveFail = 23,
  CodeTargetChanged = 24,

  CodeRadioReady = 30,
  CodeRadioInitFail = 31,
  CodeRxBadFrame = 35,
  CodeCmdTx = 38,
  CodeAckRx = 39,
  CodeTxNoAck = 40,
  CodeAnnounceRx = 41,
  CodeAckIgnored = 42,

  CodeButtonPressed = 60,
  CodeNothingToDo = 61,
};

void logInit();

void logWrite(uint8_t level, uint8_t tag, uint8_t code, int32_t arg);

#ifndef LOG_COMPILE_LEVEL
#define LOG_COMPILE_LEVEL 5
#endif

// Records above LOG_COMPILE_LEVEL are not filtered, they are not compiled - so
// they cannot switch themselves back on.
#define LOG_AT(level, tag, code, arg)                                          \
  do {                                                                         \
    if ((level) <= LOG_COMPILE_LEVEL) logWrite((level), (tag), (code), (arg)); \
  } while (0)

#define LOG_ERROR(tag, code, arg) LOG_AT(LevelError, tag, code, arg)
#define LOG_WARN(tag, code, arg) LOG_AT(LevelWarn, tag, code, arg)
#define LOG_INFO(tag, code, arg) LOG_AT(LevelInfo, tag, code, arg)
#define LOG_DEBUG(tag, code, arg) LOG_AT(LevelDebug, tag, code, arg)
