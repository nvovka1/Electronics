#include <Arduino.h>

#include "config.h"
#include "event_buffer.h"
#include "led_task.h"
#include "log.h"
#include "net_task.h"
#include "radio_task.h"
#include "settings.h"
#include "shell.h"
#include "state_task.h"
#include "ui_task.h"

// Initiator - explosion node.
//
// Receives Init/Arm/Fire/Safe over LoRa from the handheld controller and from
// the backend's queue, runs the state machine, shows the state on four LEDs and
// an OLED, and reports every transition.
//
// Five tasks, one responsibility each, talking only through queues:
//
//   radio  owns the SX1276      decode, check address and counter, ACK
//   state  owns THE STATE       apply the table, run the countdown
//   led    owns the four LEDs   light exactly one
//   ui     owns the OLED        draw
//   net    owns WiFi and HTTP   report, poll, drain the buffers
//
// Nothing is shared between them, which is why there is not a mutex anywhere in
// this firmware.

void setup() {
  Serial.begin(115200);

  logInit();
  eventBufferInit();

  LOG_INFO(TagSys, CodeBoot, 0);

  // Logged at every boot, and at WARN when it was not a clean one. A node that
  // restarts on its own comes back in SAFE, so from the outside it looks like
  // the state machine misbehaving rather than like the board dying - this line
  // is what tells the two apart, and it goes to the dashboard too.
  const esp_reset_reason_t resetReason = esp_reset_reason();
  const bool cleanStart =
      resetReason == ESP_RST_POWERON || resetReason == ESP_RST_EXT || resetReason == ESP_RST_SW;

  LOG_AT(cleanStart ? LevelInfo : LevelWarn, TagSys, CodeResetReason, (int32_t)resetReason);

  // Before any task: everything below reads these.
  settingsLoad();

  // Order matters once. The LEDs come up first so the board shows SAFE from the
  // earliest possible moment - a dark board is not one of the four things a LED
  // here is allowed to mean. Then the display, then the radio, then the state
  // task which drives both. The network is last because it is the only one
  // nothing else waits for.
  ledTaskStart();
  uiTaskStart();
  radioTaskStart();
  stateTaskStart();
  netTaskStart();
  shellTaskStart();
}

void loop() {
  // Everything runs in its own task. Arduino's loop task has nothing to do, and
  // spinning here would starve the others of the core it sits on.
  vTaskDelay(portMAX_DELAY);
}
