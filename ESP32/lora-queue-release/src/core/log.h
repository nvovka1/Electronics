#pragma once

#include <Arduino.h>

#include "ring.h"

// The log has three readers and they want different things:
//
//   you, at the desk       everything at once, as text, speed irrelevant
//   you, in a month        whatever fitted in the ring when the node came back
//   the gateway, now       WARN and above only, as numbers, rarely
//
// So a call site records a code, not a sentence. `LOG_W(TAG_RADIO, E_TX_NOACK,
// seq)` costs twelve bytes in the ring and about two microseconds; the same
// fact as a formatted line costs about 62 bytes and, over a blocking UART at
// 115200, about seven milliseconds. The text is reconstructed on the desk from
// docs/log_dict.csv.

enum LogLevel : uint8_t {
  LVL_PANIC = 0, // cannot continue
  LVL_ERROR = 1, // the device is not doing its job
  LVL_WARN = 2,  // working, but worse than it should
  LVL_INFO = 3,  // a state change worth remembering
  LVL_DEBUG = 4, // detail
  LVL_TRACE = 5, // step by step
};

enum LogTag : uint8_t {
  TAG_SYS = 0,
  TAG_POST = 1,
  TAG_CFG = 2,
  TAG_RADIO = 3,
  TAG_UI = 4,
  TAG_KEY = 5,
  TAG_BATT = 6,
  TAG_COUNT = 7,
};

// Event codes. These are the protocol between the firmware and
// docs/log_dict.csv: change a number here and the dictionary must change with
// it, or a dump from an older node decodes to the wrong story.
enum LogCode : uint8_t {
  E_NONE = 0,
  // sys 1..9
  E_BOOT = 1,               // arg = reset reason
  E_BOOT_COUNT = 2,         // arg = consecutive abnormal boots
  E_FW_DIRTY = 3,           // arg = 0; this image was built with local edits
  E_SAFE_MODE = 4,          // arg = boot count that triggered it
  E_TASK_START_FAIL = 5,    // arg = task index
  E_UPTIME_CLEAN = 6,       // arg = seconds; the boot counter has been cleared
  E_WDT_SUBSCRIBED = 7,     // arg = timeout in seconds
  // post 10..19
  E_POST_FAIL = 10,         // arg = failing bit index
  E_POST_PASS = 11,         // arg = mask (0)
  E_POST_MASK = 12,         // arg = full mask
  E_POST_CRITICAL = 13,     // arg = failing bit index of a critical block
  // cfg 20..29
  E_CFG_LOADED = 20,        // arg = (slot << 24) | seq
  E_CFG_DEFAULTS = 21,      // arg = 0; nothing valid in NVS
  E_CFG_MIGRATED = 22,      // arg = (from << 16) | to
  E_CFG_CHANGED = 23,       // arg = (field index << 24) | new value
  E_CFG_SAVED = 24,         // arg = (slot << 24) | seq
  E_CFG_SAVE_FAIL = 25,     // arg = esp_err_t
  E_CFG_SLOT_BAD = 26,      // arg = slot index whose CRC did not match
  E_CFG_RESET = 27,         // arg = 0
  E_CFG_INVALID = 28,       // arg = 0; stored config failed validation
  // radio 30..39
  E_RADIO_INIT_FAIL = 30,   // arg = attempt number
  E_TX = 31,                // arg = seq
  E_RX = 32,                // arg = (src << 16) | seq
  E_TX_NOACK = 33,          // arg = seq
  E_RX_BAD_FRAME = 34,      // arg = frame_result_t
  E_RX_DUP = 35,            // arg = (src << 16) | seq
  E_ACK_RX = 36,            // arg = rssi (sign-extended)
  E_HEALTH_TX = 37,         // arg = post mask
  E_RADIO_READY = 38,       // arg = frequency in kHz
  E_TX_AIRTIME = 39,        // arg = milliseconds the symbol took to send
  // batt 40..49
  E_LOWBAT_WRITE_BLOCKED = 40, // arg = millivolts
  E_BATT_LOW = 41,             // arg = millivolts
  E_BATT_UNTRUSTED = 42,       // arg = 0; the ADC check failed, gate disabled
  // queues 50..59
  E_QUEUE_FULL = 50,        // arg = queue index
};

// Every level a call site can use gets its own macro, and the ones above
// LOG_COMPILE_LEVEL expand to nothing. In the field image DEBUG and TRACE are
// therefore not switched off by a flag - they are absent from the binary, so
// they can never switch themselves back on.
#ifndef LOG_COMPILE_LEVEL
#define LOG_COMPILE_LEVEL 5
#endif

#ifndef LOG_TO_UART
#define LOG_TO_UART 0
#endif

void logBegin();

// Safe to call from anywhere, interrupt context included: all it ever does
// unconditionally is push twelve bytes into the ring under a spinlock.
void logPut(uint8_t level, uint8_t tag, uint8_t code, uint32_t arg);

#define LOG_P(tag, code, arg) logPut(LVL_PANIC, (tag), (code), (uint32_t)(arg))
#define LOG_E(tag, code, arg) logPut(LVL_ERROR, (tag), (code), (uint32_t)(arg))

#if LOG_COMPILE_LEVEL >= 2
#define LOG_W(tag, code, arg) logPut(LVL_WARN, (tag), (code), (uint32_t)(arg))
#else
#define LOG_W(tag, code, arg) ((void)0)
#endif

#if LOG_COMPILE_LEVEL >= 3
#define LOG_I(tag, code, arg) logPut(LVL_INFO, (tag), (code), (uint32_t)(arg))
#else
#define LOG_I(tag, code, arg) ((void)0)
#endif

#if LOG_COMPILE_LEVEL >= 4
#define LOG_D(tag, code, arg) logPut(LVL_DEBUG, (tag), (code), (uint32_t)(arg))
#else
#define LOG_D(tag, code, arg) ((void)0)
#endif

#if LOG_COMPILE_LEVEL >= 5
#define LOG_T(tag, code, arg) logPut(LVL_TRACE, (tag), (code), (uint32_t)(arg))
#else
#define LOG_T(tag, code, arg) ((void)0)
#endif

// The runtime threshold, which comes from the config. One node in the field
// can be made verbose with `config set log_level 4` and no reflash - within
// whatever the image was compiled to carry.
void logSetLevel(uint8_t level);
uint8_t logGetLevel();

uint16_t logCount();
uint32_t logDropped();
bool logPeek(uint16_t index, log_rec_t &out); // 0 = newest
void logClear();

const char *logLevelName(uint8_t level);
const char *logTagName(uint8_t tag);
const char *logCodeName(uint8_t code);

// Newest first: the thirty records before a fall are the valuable part, not
// the ones around it.
void logDump(Print &out, uint16_t count);
