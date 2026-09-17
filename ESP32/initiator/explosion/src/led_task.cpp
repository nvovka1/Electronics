#include "led_task.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "board_pins.h"
#include "config.h"
#include "log.h"

namespace {

QueueHandle_t ledQueue = nullptr;

// Indexed by node_state_t, so the lookup is the enum itself rather than a
// switch that can fall out of step with it.
const uint8_t pinForState[STATE_COUNT] = {
    LedSafePin,  // STATE_SAFE  - blue
    LedInitPin,  // STATE_INIT  - green
    LedArmedPin, // STATE_ARMED - yellow
    LedFirePin,  // STATE_FIRE  - red
};

void show(uint8_t state) {
  if (state >= STATE_COUNT) return;

  // Every LED is driven on every update, not just the one that changed. It
  // costs four writes and it means the board cannot end up showing two states
  // at once after a missed message.
  for (uint8_t i = 0; i < STATE_COUNT; i++) {
    digitalWrite(pinForState[i], i == state ? HIGH : LOW);
  }
}

void ledTask(void *) {
  LedRequest request;

  for (;;) {
    if (xQueueReceive(ledQueue, &request, portMAX_DELAY) == pdTRUE) {
      show(request.state);
    }
  }
}

} // namespace

void ledTaskStart() {
  for (uint8_t i = 0; i < STATE_COUNT; i++) {
    pinMode(pinForState[i], OUTPUT);
    digitalWrite(pinForState[i], LOW);
  }

  // SAFE immediately, before the task even starts. The board must never sit
  // dark between power-on and the first message: dark is not one of the four
  // things a LED here is allowed to mean.
  show(STATE_SAFE);

  ledQueue = xQueueCreate(LedQueueDepth, sizeof(LedRequest));

  if (ledQueue == nullptr ||
      xTaskCreate(ledTask, "led", LedTaskStack, nullptr, LedTaskPriority, nullptr) !=
          pdPASS) {
    LOG_ERROR(TagSys, CodeTaskStartFail, 0);
  }
}

void ledPost(const LedRequest &request) {
  if (ledQueue == nullptr) return;
  xQueueSend(ledQueue, &request, 0);
}
