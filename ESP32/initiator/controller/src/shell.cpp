#include "shell.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "board_pins.h"
#include "command_task.h"
#include "config.h"
#include "log.h"
#include "radio_task.h"
#include "settings.h"

namespace {

char line[ShellLineMax];
uint8_t length = 0;

// Watches the raw pin levels and reports every change. Answers the one question
// the rest of the log cannot: is the button reaching the pin at all?
//
// A press that shows here but produces no "button" line is a firmware problem.
// A press that does not show here never got to the chip, and no amount of
// firmware will find it.
void watchPins() {
  struct Watched {
    const char *name;
    uint8_t pin;
  };

  const Watched watched[] = {
      {"SEQ ", ButtonSequencePin},
      {"SAFE", ButtonSafePin},
      {"TGT ", ButtonTargetPin},
  };

  constexpr size_t Count = sizeof(watched) / sizeof(watched[0]);
  constexpr uint32_t WatchMs = 8000;

  bool last[Count];

  Serial.println(F("watching for 8 s - press the buttons now"));

  for (size_t i = 0; i < Count; i++) {
    last[i] = digitalRead(watched[i].pin) == LOW;
    Serial.printf("  %s GPIO %-2u  %s\n", watched[i].name, watched[i].pin,
                  last[i] ? "LOW  (pressed, or floating low)" : "HIGH (released)");
  }

  const uint32_t until = millis() + WatchMs;

  while ((int32_t)(until - millis()) > 0) {
    for (size_t i = 0; i < Count; i++) {
      const bool down = digitalRead(watched[i].pin) == LOW;
      if (down == last[i]) continue;

      last[i] = down;
      Serial.printf("[%8lu]   %s GPIO %-2u -> %s\n", (unsigned long)millis(),
                    watched[i].name, watched[i].pin, down ? "LOW" : "HIGH");
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  Serial.println(F("done. No LOW at all means the button never reaches the pin:"));
  Serial.println(F("check it is wired between the pin and GND, and that it is that pin."));
}

void printHelp() {
  Serial.println(F("commands:"));
  Serial.println(F("  status                 target, belief, counter, radio"));
  Serial.println(F("  pins                   watch the raw button pins for 8 s"));
  Serial.println(F("  set node_id <n>        this controller's own address"));
  Serial.println(F("  set target <n>         the node to command"));
  Serial.println(F("  set targets <n>        highest node id the TARGET button cycles to"));
  Serial.println(F("  set power <2..17>      tx power, live, for finding what the link needs"));
  Serial.println(F("  reset                  settings back to defaults (not the counter)"));
}

void printStatus() {
  Serial.printf("node id   %u (this controller)\n", (unsigned)settings.nodeId);
  // Printed first and plainly, because the whole class of "it transmits and
  // nothing answers" comes down to this number being wrong. A node that is not
  // the target hears the command and says nothing, by design.
  Serial.printf("target    node %u of 1..%u   <- commands go here\n",
                (unsigned)commandTarget(), (unsigned)settings.maxTargetId);

  switch (commandBelief()) {
  case BELIEF_UNKNOWN:
    Serial.println(F("belief    unknown - nothing heard from that node yet"));
    break;
  case BELIEF_LOST:
    Serial.printf("belief    LOST - last seen %s, may have moved since\n",
                  state_name(commandBelievedState()));
    break;
  default:
    Serial.printf("belief    %s\n", state_name(commandBelievedState()));
    break;
  }

  const command_t next = sequence_next(commandBelievedState(), commandBelief());
  Serial.printf("next      %s\n", next == COMMAND_NONE ? "- (latched, press SAFE)"
                                                       : command_name(next));

  Serial.printf("counter   %lu\n", (unsigned long)counterCurrent());
  Serial.printf("radio     %s  tx %d dBm  last rssi %d dBm\n",
                radioIsReady() ? "ready" : "FAILED", radioTxPower(), radioLastRssi());
  Serial.printf("boots     %lu\n", (unsigned long)settings.bootCount);
}

void handleSet(char *arguments) {
  char *key = strtok(arguments, " ");
  char *value = strtok(nullptr, " ");

  if (key == nullptr || value == nullptr) {
    Serial.println(F("? set <what> <value>"));
    return;
  }

  if (strcmp(key, "node_id") == 0) {
    settingsSaveNodeId((uint16_t)atoi(value));
    Serial.printf("node id %u\n", (unsigned)settings.nodeId);
  } else if (strcmp(key, "target") == 0) {
    settingsSaveTargetId((uint16_t)atoi(value));
    Serial.printf("target %u\n", (unsigned)settings.targetId);
  } else if (strcmp(key, "targets") == 0) {
    settingsSaveMaxTargetId((uint16_t)atoi(value));
    Serial.printf("TARGET button cycles 1..%u\n", (unsigned)settings.maxTargetId);
  } else if (strcmp(key, "power") == 0) {
    radioSetTxPower(atoi(value));
    Serial.printf("tx power %d dBm (not saved - put the final value in config.h)\n",
                  radioTxPower());
  } else {
    Serial.println(F("? node_id|target|targets|power"));
  }
}

void execute(char *input) {
  while (*input == ' ') input++;
  if (*input == '\0') return;

  if (strcmp(input, "help") == 0) { printHelp(); return; }
  if (strcmp(input, "status") == 0) { printStatus(); return; }
  if (strcmp(input, "pins") == 0) { watchPins(); return; }

  if (strcmp(input, "reset") == 0) {
    settingsReset();
    Serial.println(F("settings back to defaults; the counter is untouched"));
    return;
  }

  if (strncmp(input, "set ", 4) == 0) { handleSet(input + 4); return; }

  Serial.println(F("? type help"));
}

void shellTask(void *) {
  Serial.println();
  Serial.printf("initiator controller %u, build %s\n", (unsigned)settings.nodeId,
                FW_BUILD_TYPE);
  Serial.println(F("type help"));

  for (;;) {
    while (Serial.available()) {
      const char character = (char)Serial.read();

      if (character == '\r') continue;

      if (character == '\n') {
        line[length] = '\0';
        execute(line);
        length = 0;
        continue;
      }

      if (length < ShellLineMax - 1) line[length++] = character;
    }

    vTaskDelay(pdMS_TO_TICKS(ShellPollMs));
  }
}

} // namespace

void shellTaskStart() {
  if (xTaskCreate(shellTask, "shell", ShellTaskStack, nullptr, ShellTaskPriority,
                  nullptr) != pdPASS) {
    LOG_ERROR(TagSys, CodeTaskStartFail, 0);
  }
}
