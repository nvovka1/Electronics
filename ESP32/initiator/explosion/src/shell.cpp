#include "shell.h"

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "config.h"
#include "event_buffer.h"
#include "log.h"
#include "net_task.h"
#include "radio_task.h"
#include "settings.h"
#include "state_task.h"

namespace {

char line[ShellLineMax];
uint8_t length = 0;

void printHelp() {
  Serial.println(F("commands:"));
  Serial.println(F("  status                 state, countdown, links, buffers"));
  Serial.println(F("  net                    wifi and service settings"));
  Serial.println(F("  set node_id <n>        this node's protocol address"));
  Serial.println(F("  set autoarm <seconds>  INIT -> ARMED timeout"));
  Serial.println(F("  set wifi <ssid> <pass>"));
  Serial.println(F("  set url <base-url>"));
  Serial.println(F("  set key <api-key>"));
  Serial.println(F("  reset                  settings back to the built-in defaults"));
#if BUILD_TEST_COMMANDS
  Serial.println(F("  cmd <init|arm|fire|safe>   bench only: drive the state machine"));
#endif
}

void printStatus() {
  Serial.printf("serial    %s\n", settings.serial);
  Serial.printf("node id   %u\n", (unsigned)settings.nodeId);
  Serial.printf("state     %s\n", state_name((node_state_t)stateCurrent()));

  const uint32_t remaining = stateCountdownRemainingMs();
  if (remaining > 0) {
    Serial.printf("auto-arm  in %lu s (of %lu)\n", (unsigned long)(remaining / 1000UL),
                  (unsigned long)settings.autoArmSeconds);
  } else {
    Serial.printf("auto-arm  not running (timeout %lu s)\n",
                  (unsigned long)settings.autoArmSeconds);
  }

  Serial.printf("lora      %s\n", radioIsReady() ? "ready" : "FAILED");
  Serial.printf("wifi      %s\n", netIsConnected() ? "up" : "down");
  Serial.printf("boots     %lu\n", (unsigned long)settings.bootCount);
  Serial.printf("events    %u waiting, %u lost to wrap\n", (unsigned)eventBufferPending(),
                (unsigned)eventBufferDropped());
  Serial.printf("logs      %u waiting, %u lost to wrap\n", (unsigned)logPending(),
                (unsigned)logDropped());
}

void printNet() {
  Serial.printf("ssid      %s\n", settings.ssid);
  Serial.printf("link      %s", netIsConnected() ? "up" : "down");
  if (netIsConnected()) {
    Serial.printf("  ip %s  rssi %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  }
  Serial.println();
  Serial.printf("service   %s\n", settings.baseUrl);
  // The key itself is never printed. Whether there is one, and how long it is,
  // is all anyone needs to tell a missing key from a wrong one.
  Serial.printf("api key   %s (%u chars)\n",
                settings.apiKey[0] != '\0' ? "set" : "NOT SET",
                (unsigned)strlen(settings.apiKey));
}

#if BUILD_TEST_COMMANDS
void handleCommand(const char *argument) {
  command_t command;

  if (strcmp(argument, "init") == 0)      command = COMMAND_INIT;
  else if (strcmp(argument, "arm") == 0)  command = COMMAND_ARM;
  else if (strcmp(argument, "fire") == 0) command = COMMAND_FIRE;
  else if (strcmp(argument, "safe") == 0) command = COMMAND_SAFE;
  else {
    Serial.println(F("? init|arm|fire|safe"));
    return;
  }

  CommandRequest request = {};
  request.command = (uint8_t)command;
  // Reported as having come from the API, because there is no fifth source and
  // inventing one would put a value in the record the backend cannot decode.
  // The log line below is what distinguishes a bench command.
  request.source = (uint8_t)SOURCE_API;

  Serial.printf("submitting %s from the cable\n", command_name(command));
  stateSubmitCommand(request);
}
#endif

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
  } else if (strcmp(key, "autoarm") == 0) {
    settingsSaveAutoArmSeconds((uint32_t)atol(value));
    Serial.printf("auto-arm %lu s (takes effect on the next INIT)\n",
                  (unsigned long)settings.autoArmSeconds);
  } else if (strcmp(key, "wifi") == 0) {
    char *password = strtok(nullptr, " ");
    settingsSaveWifi(value, password == nullptr ? "" : password);
    Serial.println(F("wifi saved"));
  } else if (strcmp(key, "url") == 0) {
    settingsSaveBaseUrl(value);
    Serial.printf("service %s\n", settings.baseUrl);
  } else if (strcmp(key, "key") == 0) {
    settingsSaveApiKey(value);
    Serial.printf("api key set (%u chars)\n", (unsigned)strlen(settings.apiKey));
  } else {
    Serial.println(F("? node_id|autoarm|wifi|url|key"));
  }
}

void execute(char *input) {
  while (*input == ' ') input++;
  if (*input == '\0') return;

  if (strcmp(input, "help") == 0) { printHelp(); return; }
  if (strcmp(input, "status") == 0) { printStatus(); return; }
  if (strcmp(input, "net") == 0) { printNet(); return; }

  if (strcmp(input, "reset") == 0) {
    settingsReset();
    Serial.println(F("settings back to defaults"));
    return;
  }

  if (strncmp(input, "set ", 4) == 0) { handleSet(input + 4); return; }

#if BUILD_TEST_COMMANDS
  if (strncmp(input, "cmd ", 4) == 0) { handleCommand(input + 4); return; }
#endif

  Serial.println(F("? type help"));
}

void shellTask(void *) {
  Serial.println();
  Serial.printf("initiator explosion node %s, build %s\n", settings.serial, FW_BUILD_TYPE);
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

      // A line longer than the buffer is truncated rather than overflowing it.
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
