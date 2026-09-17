#include <Arduino.h>

#include "button_task.h"
#include "command_task.h"
#include "config.h"
#include "log.h"
#include "radio_task.h"
#include "settings.h"
#include "shell.h"
#include "ui_task.h"

// Initiator - handheld controller.
//
// Three buttons, one job each:
//
//   SEQ     advance INIT -> ARM -> FIRE
//   SAFE    send SAFE, from any state, always
//   TARGET  cycle which node is being commanded
//
// Four tasks:
//
//   button   owns the three GPIOs   debounce, post one event per press
//   command  owns the belief        decide, send, retry, consume the answer
//   radio    owns the SX1276        transmit, decode ACKs and announcements
//   ui       owns the OLED          draw
//
// This board has no WiFi and does not talk to the backend. It does not hold the
// transition table either: it proposes the next command in the sequence, and
// the node decides. The belief on the screen changes only when the node says
// so - a controller that advances its own display on an unacknowledged send is
// a controller that lies.

void setup() {
  Serial.begin(115200);

  logInit();
  LOG_INFO(TagSys, CodeBoot, 0);

  // Before any task: everything below reads these, and the radio needs the node
  // id for the SRC field of the first frame it sends.
  settingsLoad();

  uiTaskStart();
  radioTaskStart();
  buttonTaskStart();
  commandTaskStart();
  shellTaskStart();
}

void loop() {
  // Everything runs in its own task. Spinning here would starve them of the
  // core this one sits on.
  vTaskDelay(portMAX_DELAY);
}
