#include <Arduino.h>

#include "config.h"
#include "log.h"
#include "log_task.h"
#include "mav_task.h"
#include "net_task.h"
#include "settings.h"
#include "shell.h"
#include "store.h"
#include "web_task.h"

// MAVLink flight telemetry logger.
//
// An ESP32 on a spare UART of a SpeedyBee F405 V3 running ArduPilot. It decodes
// the telemetry stream, writes a CSV row twice a second to its own flash, serves
// those flights over WiFi, and pushes them to the Initiator service so the same
// CSV can be downloaded from the site.
//
// Five tasks, one responsibility each:
//
//   mav    owns UART2           decode into the snapshot, request streams
//   rec    owns the open file   one row per tick, whatever else is happening
//   net    owns WiFi and HTTP   join, upload, move the cursor
//   web    owns port 80         the board's own page and the CSV download
//   shell  owns the console     commissioning
//
// Only two things are shared - the snapshot and the flight log - and both live
// behind their own lock in store.cpp. Nothing else in this firmware needs one.
//
// The ordering below matters in one place: the MAVLink task starts before the
// network. A board that cannot join WiFi should still be recording, and
// starting the radio first would delay the first row by the twenty seconds it
// takes to give up on a network that is not there.

void setup() {
  Serial.begin(115200);
  delay(100);

  logInit();

  const esp_reset_reason_t resetReason = esp_reset_reason();
  const bool cleanStart = resetReason == ESP_RST_POWERON || resetReason == ESP_RST_EXT ||
                          resetReason == ESP_RST_SW;

  // A board that restarts on its own starts a new flight file, so from the
  // outside the symptom is a flight that ends abruptly and another that begins.
  // This line is what tells that apart from the aircraft being switched off.
  logPrintf(cleanStart ? LevelInfo : LevelWarn, "sys", "boot, reset reason %d",
            (int)resetReason);

  settingsLoad();

  if (!storeBegin()) {
    // Worth continuing. A board with no filesystem still decodes the link and
    // still serves its page, which is enough to diagnose the filesystem.
    LOG_ERROR("sys", "storage unavailable, recording disabled");
  }

  mavTaskStart();
  logTaskStart();
  netTaskStart();
  webTaskStart();
  shellTaskStart();
}

void loop() {
  // Everything is in a task. Arduino's loop runs at the lowest priority on core
  // 1 and would otherwise spin.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
