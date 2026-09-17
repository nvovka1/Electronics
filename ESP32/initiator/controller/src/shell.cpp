#include "shell.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "command_task.h"
#include "config.h"
#include "log.h"
#include "radio_task.h"
#include "settings.h"

namespace {

char line[ShellLineMax];
uint8_t length = 0;

void printHelp() {
  Serial.println(F("commands:"));
  Serial.println(F("  status                 target, belief, counter, radio"));
  Serial.println(F("  set node_id <n>        this controller's own address"));
  Serial.println(F("  set target <n>         the node to command"));
  Serial.println(F("  set targets <n>        highest node id the TARGET button cycles to"));
  Serial.println(F("  reset                  settings back to defaults (not the counter)"));
}

void printStatus() {
  Serial.printf("node id   %u (this controller)\n", (unsigned)settings.nodeId);
  Serial.printf("target    %u of 1..%u\n", (unsigned)commandTarget(),
                (unsigned)settings.maxTargetId);

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
  Serial.printf("radio     %s  rssi %d dBm\n", radioIsReady() ? "ready" : "FAILED",
                radioLastRssi());
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
    Serial.printf("cycling 1..%u\n", (unsigned)settings.maxTargetId);
  } else {
    Serial.println(F("? node_id|target|targets"));
  }
}

void execute(char *input) {
  while (*input == ' ') input++;
  if (*input == '\0') return;

  if (strcmp(input, "help") == 0) { printHelp(); return; }
  if (strcmp(input, "status") == 0) { printStatus(); return; }

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
