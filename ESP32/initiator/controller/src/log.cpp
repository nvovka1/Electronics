#include "log.h"

#include <Arduino.h>

#include "sequence.h"

// This log goes to the serial line and nowhere else - there is no backend on
// this board and nothing reconstructs codes into sentences afterwards. So it
// prints words.
//
// The names cost flash, which is why they are compiled only when LOG_TO_UART is
// set. A field build has nowhere to print them and does not carry them.

namespace {

#if LOG_TO_UART

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
  case TagSys:    return "sys";
  case TagCfg:    return "cfg";
  case TagRadio:  return "radio";
  case TagButton: return "button";
  case TagUi:     return "ui";
  default:        return "?";
  }
}

const char *codeName(uint8_t code) {
  switch (code) {
  case CodeBoot:          return "boot";
  case CodeBootCount:     return "boot_count";
  case CodeTaskStartFail: return "task_start_fail";

  case CodeCfgLoaded:     return "cfg_loaded";
  case CodeCfgDefaults:   return "cfg_defaults";
  case CodeCfgSaved:      return "cfg_saved";
  case CodeCfgSaveFail:   return "cfg_save_fail";
  case CodeTargetChanged: return "target_changed";

  case CodeRadioReady:    return "radio_ready";
  case CodeRadioInitFail: return "radio_init_FAIL";
  case CodeRxBadFrame:    return "rx_bad_frame";
  case CodeCmdTx:         return "cmd_tx";
  case CodeAckRx:         return "ack_rx";
  case CodeTxNoAck:       return "tx_NO_ACK";
  case CodeAnnounceRx:    return "announce_rx";
  case CodeAckIgnored:    return "ack_ignored";
  case CodeCmdAccepted:   return "cmd_ACCEPTED";
  case CodeCmdRefused:    return "cmd_REFUSED";
  case CodeCmdLost:       return "cmd_LOST";
  case CodeRadioBusy:     return "radio_not_ready";
  case CodeSafeAbort:     return "SAFE_aborts_retry";
  case CodePressDropped:  return "press_DROPPED";
  case CodeNoPullup:      return "NO_PULLUP";

  case CodeButtonPressed: return "button";
  case CodeNothingToDo:   return "nothing_to_do";
  default:                return "?";
  }
}

const char *buttonName(int32_t id) {
  switch (id) {
  case 0:  return "SEQ";
  case 1:  return "SAFE";
  case 2:  return "TARGET";
  default: return "?";
  }
}

// The argument means whatever its code documents, so it is rendered per code
// rather than printed as a bare number. "button 2" is a lookup every time;
// "button TARGET" is not.
void printArg(uint8_t code, int32_t arg) {
  switch (code) {
  case CodeButtonPressed:
    Serial.print(buttonName(arg));
    break;

  case CodeCmdTx:
    Serial.print(command_name((command_t)arg));
    break;

  case CodeAckRx:
  case CodeAnnounceRx:
    Serial.printf("node is %s", state_name((node_state_t)arg));
    break;

  case CodeTxNoAck:
    Serial.printf("attempt %ld", (long)arg);
    break;

  case CodeCmdAccepted:
    Serial.printf("node is %s", state_name((node_state_t)arg));
    break;

  case CodeCmdRefused:
    Serial.printf("%s", reason_name((reject_reason_t)arg));
    break;

  case CodeCmdLost:
    Serial.printf("%ld attempts, belief now stale", (long)arg);
    break;

  case CodeSafeAbort:
    Serial.printf("dropped %s to let SAFE through", command_name((command_t)arg));
    break;

  case CodePressDropped:
    Serial.printf("%s - queue full, press LOST", buttonName(arg));
    break;

  case CodeNoPullup:
    Serial.printf("GPIO %ld reads low at boot - missing 10k to 3V3?", (long)arg);
    break;

  case CodeAckIgnored:
    Serial.printf("counter %ld", (long)arg);
    break;

  case CodeTargetChanged:
    Serial.printf("now node %ld", (long)arg);
    break;

  case CodeNothingToDo:
    Serial.printf("believed %s", state_name((node_state_t)arg));
    break;

  case CodeBootCount:
    Serial.printf("%ld boots", (long)arg);
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

void logInit() {}

void logWrite(uint8_t level, uint8_t tag, uint8_t code, int32_t arg) {
#if LOG_TO_UART
  Serial.printf("[%8lu] %s %-6s %-16s ", (unsigned long)millis(), levelName(level),
                tagName(tag), codeName(code));
  printArg(code, arg);
  Serial.println();
#else
  // Nowhere to put these in a field build. Referenced so every call site does
  // not warn about an unused argument.
  (void)level;
  (void)tag;
  (void)code;
  (void)arg;
#endif
}
