#include "ui_task.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "board_pins.h"
#include "config.h"
#include "log.h"
#include "radio_task.h"
#include "settings.h"

namespace {

Adafruit_SSD1306 display(OledWidth, OledHeight, &Wire, OledResetPin);

QueueHandle_t uiQueue = nullptr;
UiUpdate latest = {};
uint32_t latestAtMs = 0;
bool displayReady = false;

// What the sequence button would send next, so the operator can see what is
// about to happen before pressing it.
const char *nextCommandLabel() {
  const command_t next =
      sequence_next((node_state_t)latest.believedState, (belief_t)latest.belief);

  return next == COMMAND_NONE ? "-" : command_name(next);
}

void drawState() {
  display.setTextSize(3);
  display.setCursor(0, 0);

  // A belief that is stale or absent is shown as such rather than as a state.
  // Printing SAFE when we do not know would be the single most dangerous thing
  // this screen could do.
  if (latest.belief == BELIEF_UNKNOWN) {
    display.print("?");
  } else if (latest.belief == BELIEF_LOST) {
    display.print("LOST");
  } else {
    display.print(state_name((node_state_t)latest.believedState));
  }
}

void drawResult() {
  display.setTextSize(1);
  display.setCursor(0, 28);

  // The line clears itself after a while, so an old outcome cannot be read as a
  // current one.
  if (latest.result != ResultNone &&
      (uint32_t)(millis() - latestAtMs) > ResultLingerMs) {
    return;
  }

  switch (latest.result) {
  case ResultAccepted:
    display.printf("%s ok", command_name((command_t)latest.lastCommand));
    if (latest.announced) display.print(" (node)");
    break;

  case ResultRefused:
    display.printf("%s REFUSED", command_name((command_t)latest.lastCommand));
    display.setCursor(0, 38);
    display.print(reason_name((reject_reason_t)latest.reason));
    break;

  case ResultLost:
    display.printf("%s no answer", command_name((command_t)latest.lastCommand));
    display.setCursor(0, 38);
    display.printf("%u tries", (unsigned)latest.attempts);
    break;

  case ResultNothingToDo:
    display.print("latched - press SAFE");
    break;

  default:
    break;
  }
}

void draw() {
  if (!displayReady) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  drawState();
  drawResult();

  // Announcements arrive without anyone pressing anything, so the screen says
  // when the node moved on its own - otherwise the state appears to change by
  // itself.
  if (latest.announced && latest.result != ResultRefused) {
    display.setTextSize(1);
    display.setCursor(0, 48);
    display.print("node armed itself");
  }

  display.setTextSize(1);
  display.setCursor(0, 56);
  display.printf("->n%u next %s %s %ddBm", (unsigned)latest.targetId, nextCommandLabel(),
                 radioIsReady() ? "" : "NORADIO", latest.rssi);

  display.display();
}

void uiTask(void *) {
  UiUpdate update;

  for (;;) {
    if (xQueueReceive(uiQueue, &update, pdMS_TO_TICKS(UiRefreshMs)) == pdTRUE) {
      latest = update;
      latestAtMs = millis();
    }

    draw();
  }
}

} // namespace

void uiTaskStart() {
  Wire.begin(OLED_SDA, OLED_SCL);

  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OledI2cAddress);

  if (!displayReady) {
    // A controller with a dead screen is nearly useless - it is the only output
    // this board has - but it can still send, so it still runs.
    LOG_ERROR(TagUi, CodeRadioInitFail, 0);
  } else {
    display.clearDisplay();
    display.display();
  }

  uiQueue = xQueueCreate(UiQueueDepth, sizeof(UiUpdate));

  if (uiQueue == nullptr ||
      xTaskCreate(uiTask, "ui", UiTaskStack, nullptr, UiTaskPriority, nullptr) != pdPASS) {
    LOG_ERROR(TagSys, CodeTaskStartFail, 0);
  }
}

void uiPost(const UiUpdate &update) {
  if (uiQueue == nullptr) return;
  xQueueSend(uiQueue, &update, 0);
}
