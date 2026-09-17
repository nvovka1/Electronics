#include "net_task.h"

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "event_buffer.h"
#include "fleet_client.h"
#include "log.h"
#include "settings.h"
#include "state_task.h"
#include "ui_task.h"

namespace {

bool connected = false;
uint32_t lastHealthMs = 0;
uint32_t lastPollMs = 0;
uint32_t lastWifiAttemptMs = 0;

// True once `since` has elapsed. Signed arithmetic, so it stays correct across
// the millis() wrap at 49 days rather than going quiet for the next 49.
bool elapsed(uint32_t since, uint32_t interval) {
  return (int32_t)(millis() - since) >= (int32_t)interval;
}

void ensureWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!connected) {
      connected = true;
      LOG_INFO(TagNet, CodeWifiUp, WiFi.RSSI());
      uiSetWifiUp(true);
    }
    return;
  }

  if (connected) {
    connected = false;
    LOG_WARN(TagNet, CodeWifiDown, 0);
    uiSetWifiUp(false);
  }

  if (!elapsed(lastWifiAttemptMs, WifiRetryIntervalMs)) return;
  lastWifiAttemptMs = millis();

  if (settings.ssid[0] == '\0') return; // nothing to connect to

  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.ssid, settings.password);
}

// Drains the buffer in batches. Only what the service says it stored is
// dropped: assuming the whole batch would silently lose any record it could not
// understand.
void flushEvents() {
  StateEventRecord batch[MaxEventsPerUpload];

  while (eventBufferPending() > 0) {
    const uint8_t taken = eventBufferPeek(batch, MaxEventsPerUpload);
    if (taken == 0) break;

    uint8_t stored = 0;
    if (!fleetPostStateEvents(batch, taken, &stored)) {
      // Left in the buffer, to go again next time round. This is the whole
      // point of peek-then-commit.
      LOG_WARN(TagNet, CodeEventBuffered, eventBufferPending());
      return;
    }

    // The whole batch is dropped, not just the part that was stored. The
    // service has seen all of them; any it did not keep carried a value it
    // could not understand, and sending those again would loop forever. The
    // shortfall is logged so it is a known loss rather than a silent one.
    eventBufferCommit(taken);

    if (stored < taken) {
      LOG_WARN(TagNet, CodeEventBuffered, (int32_t)(taken - stored));
    }
  }
}

void flushLogs() {
  LogRecord batch[MaxLogsPerUpload];

  const uint8_t taken = logTake(batch, MaxLogsPerUpload);
  if (taken == 0) return;

  // Logs are taken rather than peeked: unlike a transition, a lost log line is
  // not part of the record of what the node did, and holding them through an
  // outage would push out newer ones that matter more.
  fleetPostLogs(batch, taken);
}

void pollForCommand() {
  PolledCommand polled;
  if (!fleetPollCommand(&polled)) return;

  CommandRequest request = {};
  request.command = polled.command;
  request.source = (uint8_t)SOURCE_API;
  strncpy(request.commandId, polled.commandId, COMMAND_ID_MAX - 1);

  // Onto the same queue the radio uses, so the two sources serialise and the
  // state machine judges this one against whatever state the node is actually
  // in by the time it is read.
  stateSubmitCommand(request);
}

void netTask(void *) {
  for (;;) {
    ensureWifi();

    if (connected) {
      if (elapsed(lastHealthMs, HealthReportIntervalMs)) {
        lastHealthMs = millis();

        uint8_t serviceState = stateCurrent();
        if (fleetPostHealth(stateCurrent(), &serviceState) &&
            serviceState != stateCurrent()) {
          // The service and this node disagree. The node is right - it holds
          // the state - so this is only worth a line, and the next state event
          // or report settles it.
          LOG_WARN(TagNet, CodeReportOk, logPack(stateCurrent(), serviceState));
        }

        flushEvents();
        flushLogs();
      }

      if (elapsed(lastPollMs, CommandPollIntervalMs)) {
        lastPollMs = millis();
        pollForCommand();
      }
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

} // namespace

void netTaskStart() {
  if (xTaskCreate(netTask, "net", NetTaskStack, nullptr, NetTaskPriority, nullptr) !=
      pdPASS) {
    LOG_ERROR(TagSys, CodeTaskStartFail, 0);
  }
}

bool netIsConnected() { return connected; }
