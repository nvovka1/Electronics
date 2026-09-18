#include "log.h"

#include <Arduino.h>

#include "config.h"
#include "node_state.h"

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

// The records that go to the backend stay numeric - that is the contract with
// LogDictionary.cs and it must not change. What goes to the serial line is for
// a person standing at the board, so it gets words. The names are compiled only
// when LOG_TO_UART is set, so a field image does not carry them.

const char *levelName(uint8_t level) {
  switch (level) {
  case LevelPanic: return "PANIC";
  case LevelError: return "ERROR";
  case LevelWarn:  return "WARN ";
  case LevelInfo:  return "INFO ";
  case LevelDebug: return "DEBUG";
  default:         return "TRACE";
  }
}

const char *tagName(uint8_t tag) {
  switch (tag) {
  case TagSys:   return "sys";
  case TagPost:  return "post";
  case TagCfg:   return "cfg";
  case TagRadio: return "radio";
  case TagState: return "STATE";
  case TagLed:   return "led";
  case TagUi:    return "ui";
  case TagNet:   return "net";
  case TagBatt:  return "batt";
  default:       return "?";
  }
}

const char *codeName(uint8_t code) {
  switch (code) {
  case CodeBoot:               return "boot";
  case CodeBootCount:          return "boot_count";
  case CodeFwDirty:            return "fw_dirty";
  case CodeTaskStartFail:      return "task_start_FAIL";
  case CodeResetReason:        return "reset_reason";

  case CodePostPass:           return "post_pass";
  case CodePostFail:           return "post_FAIL";
  case CodePostMask:           return "post_mask";

  case CodeCfgLoaded:          return "cfg_loaded";
  case CodeCfgDefaults:        return "cfg_defaults";
  case CodeCfgSaved:           return "cfg_saved";
  case CodeCfgSaveFail:        return "cfg_save_FAIL";
  case CodeCfgChanged:         return "cfg_changed";

  case CodeRadioReady:         return "radio_ready";
  case CodeRadioInitFail:      return "radio_init_FAIL";
  case CodeCmdRx:              return "cmd_rx";
  case CodeCmdNotForUs:        return "cmd_not_for_us";
  case CodeCmdReplay:          return "cmd_REPLAY";
  case CodeRxBadFrame:         return "rx_bad_frame";
  case CodeAckTx:              return "ack_tx";
  case CodeAnnounceTx:         return "announce_tx";
  case CodeCmdTx:              return "cmd_tx";
  case CodeAckRx:              return "ack_rx";
  case CodeTxNoAck:            return "tx_no_ack";

  case CodeStateEnter:         return "state_enter";
  case CodeCmdAccepted:        return "cmd_accepted";
  case CodeCmdRejected:        return "cmd_REJECTED";
  case CodeCountdownStarted:   return "countdown_started";
  case CodeCountdownRestarted: return "countdown_restarted";
  case CodeCountdownCancelled: return "countdown_cancelled";
  case CodeAutoArm:            return "AUTO_ARM";

  case CodeWifiUp:             return "wifi_up";
  case CodeWifiDown:           return "wifi_DOWN";
  case CodeReportOk:           return "report_ok";
  case CodeReportFail:         return "report_FAIL";
  case CodeCmdPolled:          return "cmd_polled";
  case CodeEventPosted:        return "event_posted";
  case CodeEventBuffered:      return "event_buffered";
  case CodeLogSent:            return "log_sent";
  case CodeWifiHeldOff:        return "wifi_HELD_OFF";
  case CodeWifiTxPower:        return "wifi_tx_power";
  case CodeWifiRetryAfterHold: return "wifi_retry_after_hold";
  case CodeReportTry:          return "report_TRY";
  case CodeHeapFree:           return "heap_free";
  case CodeNetAlive:           return "net_alive";

  case CodeBattLow:            return "batt_low";
  case CodeBattUntrusted:      return "batt_untrusted";
  default:                     return "?";
  }
}

// The one number worth spelling out. "nothing in the portal" is almost always
// one of these, and they mean completely different things.
void printHttp(int32_t status) {
  switch (status) {
  case -1:  Serial.print("no connection (wifi down, DNS, or TLS)"); break;
  case 401: Serial.print("401 - the API key is wrong or missing"); break;
  case 404: Serial.print("404 - wrong URL or wrong path"); break;
  case 400: Serial.print("400 - the service rejected the body"); break;
  default:
    if (status < 0) Serial.printf("HTTPClient error %ld", (long)status);
    else Serial.printf("HTTP %ld", (long)status);
    break;
  }
}

void printArg(uint8_t code, int32_t arg) {
  switch (code) {
  case CodeStateEnter:
  case CodeAutoArm:
    Serial.print(state_name((node_state_t)arg));
    break;

  // Two values in one record, because two records can be split by a ring wrap
  // and then only half the story survives.
  // Spelled out rather than "INIT -> INIT", which is the command on the left
  // and the resulting state on the right but reads as "nothing changed" - and
  // for INIT the two words are the same, so it reads wrong exactly when it
  // matters most.
  case CodeCmdAccepted:
    Serial.printf("%s accepted, node now %s", command_name((command_t)(arg & 0xFF)),
                  state_name((node_state_t)((arg >> 8) & 0xFF)));
    break;

  case CodeCmdRejected:
    Serial.printf("%s refused: %s", command_name((command_t)(arg & 0xFF)),
                  reason_name((reject_reason_t)((arg >> 8) & 0xFF)));
    break;

  case CodeCmdRx:
  case CodeCmdTx:
  case CodeCmdPolled:
    Serial.print(command_name((command_t)arg));
    break;

  case CodeCmdNotForUs:
    Serial.printf("addressed to node %ld", (long)arg);
    break;

  case CodeCmdReplay:
    Serial.printf("counter %ld already seen", (long)arg);
    break;

  case CodeCountdownStarted:
  case CodeCountdownRestarted:
    Serial.printf("%ld s to auto-arm", (long)arg);
    break;

  case CodeCountdownCancelled:
    Serial.printf("%ld s left", (long)arg);
    break;

  case CodeReportFail:
    printHttp(arg);
    break;

  case CodeReportTry:
    Serial.printf("sending, %ld byte body", (long)arg);
    break;

  case CodeHeapFree:
    Serial.printf("%ld bytes free (TLS wants ~45k)", (long)arg);
    break;

  case CodeNetAlive:
    Serial.printf("stack headroom %ld bytes", (long)arg);
    break;

  case CodeWifiUp:
    Serial.printf("rssi %ld dBm", (long)arg);
    break;

  case CodeWifiTxPower:
    Serial.printf("%ld dBm (read back from the driver)", (long)arg);
    break;

  case CodeWifiHeldOff:
    Serial.print("last reset was a BROWNOUT - not switching WiFi on this boot. "
                 "Fix the supply, then power-cycle, or type: wifi on");
    break;

  case CodeEventPosted:
  case CodeLogSent:
    Serial.printf("%ld record(s)", (long)arg);
    break;

  case CodeEventBuffered:
    Serial.printf("%ld waiting to go", (long)arg);
    break;

  case CodePostMask:
    if (arg == 0) Serial.print("clean");
    else Serial.printf("0x%02lX", (long)arg);
    break;

  case CodeBootCount:
    Serial.printf("%ld boots", (long)arg);
    break;

  // Why the board restarted. BROWNOUT is the one that matters: it means the
  // supply sagged, not that the firmware faulted, and every other symptom that
  // follows - a state that resets itself, a node missing from the dashboard -
  // is downstream of it.
  case CodeResetReason:
    switch (arg) {
    case ESP_RST_POWERON:  Serial.print("power-on"); break;
    case ESP_RST_SW:       Serial.print("software restart"); break;
    case ESP_RST_PANIC:    Serial.print("PANIC - firmware crashed"); break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      Serial.print("WATCHDOG - something stopped feeding it"); break;
    case ESP_RST_BROWNOUT: Serial.print("BROWNOUT - the 3V3 rail sagged, this is a power fault"); break;
    case ESP_RST_DEEPSLEEP: Serial.print("woke from deep sleep"); break;
    case ESP_RST_EXT:      Serial.print("external reset pin"); break;
    default:               Serial.printf("reason %ld", (long)arg); break;
    }
    break;

  case CodeBattLow:
  case CodeBattUntrusted:
    Serial.printf("%ld.%ld V", (long)(arg / 10), (long)(arg % 10));
    break;

  case CodeRxBadFrame:
    Serial.printf("reason %ld", (long)arg);
    break;

  default:
    if (arg != 0) Serial.print(arg);
    break;
  }
}

#endif // LOG_TO_UART

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
  Serial.printf("[%8lu] %s %-5s %-20s ", (unsigned long)now, levelName(level),
                tagName(tag), codeName(code));
  printArg(code, arg);
  Serial.println();
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
