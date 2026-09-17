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
#include "settings.h"

namespace {

Adafruit_SSD1306 display(OledWidth, OledHeight, &Wire, OledResetPin);

QueueHandle_t uiQueue = nullptr;

UiUpdate latest = {};
bool wifiUp = false;
bool radioUp = false;
bool displayReady = false;

// When the last update arrived, so the countdown can tick between messages
// rather than only when something happens. The state task owns the real
// deadline; this is only the screen counting down from what it was last told.
uint32_t latestAtMs = 0;

const char *sourceName(uint8_t source) {
  switch (source) {
  case SOURCE_LORA:  return "ctrl";
  case SOURCE_API:   return "api";
  case SOURCE_TIMER: return "timer";
  default:           return "?";
  }
}

uint32_t remainingMs() {
  if (latest.countdownMs == 0) return 0;

  const uint32_t elapsed = millis() - latestAtMs;
  return elapsed >= latest.countdownMs ? 0 : latest.countdownMs - elapsed;
}

void draw() {
  if (!displayReady) return;

  display.clearDisplay();

  // The state, as large as the screen allows. Somebody checking a node from
  // arm's length should be able to read this and nothing else.
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(state_name((node_state_t)latest.state));

  display.setTextSize(1);

  // The countdown, while one is running. Seconds, because that is the
  // resolution anyone acts on.
  const uint32_t remaining = remainingMs();
  if (remaining > 0) {
    display.setCursor(0, 26);
    display.printf("arms in %lus", (unsigned long)(remaining / 1000UL));
  }

  // What last arrived, and whether it was taken. A refusal reads as one here
  // rather than as a flag somebody has to notice.
  display.setCursor(0, 38);
  if (latest.lastSource == SOURCE_TIMER && latest.lastCommand == 0) {
    display.print("armed itself");
  } else if (latest.lastCommand != 0) {
    display.printf("%s %s %s", command_name((command_t)latest.lastCommand),
                   latest.lastAccepted ? "ok" : "REFUSED",
                   sourceName(latest.lastSource));
  } else {
    display.print("waiting");
  }

  if (!latest.lastAccepted) {
    display.setCursor(0, 48);
    display.print(reason_name((reject_reason_t)latest.lastReason));
  }

  // Bottom line: who this node is and what it can currently reach.
  display.setCursor(0, 56);
  display.printf("%s n%u %s %s", settings.serial, (unsigned)settings.nodeId,
                 radioUp ? "lora" : "----", wifiUp ? "wifi" : "----");

  display.display();
}

void uiTask(void *) {
  UiUpdate update;

  for (;;) {
    // A timeout rather than a blocking wait, so the countdown on screen keeps
    // moving while nothing is happening - which is exactly when someone is
    // watching it.
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
    // Not fatal. A node with a dead screen still runs the state machine, still
    // drives the LEDs and still reports - and the LEDs are the display that
    // matters.
    LOG_ERROR(TagUi, CodePostFail, 0);
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

void uiSetWifiUp(bool up) { wifiUp = up; }

void uiSetRadioUp(bool up) { radioUp = up; }
