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

struct StateLed {
  uint8_t pin;
  // Wired anode-to-3V3, so the pin sinks the current and LOW is lit. Only the
  // red one is, and only because it sits on GPIO 0 - see board_pins.h.
  bool activeLow;
};

// Indexed by node_state_t, so the lookup is the enum itself rather than a
// switch that can fall out of step with it.
const StateLed ledForState[STATE_COUNT] = {
    {LedSafePin, LedSafeActiveLow},   // STATE_SAFE  - blue
    {LedInitPin, LedInitActiveLow},   // STATE_INIT  - green
    {LedArmedPin, LedArmedActiveLow}, // STATE_ARMED - yellow
    {LedFirePin, LedFireActiveLow},   // STATE_FIRE  - red
};

// The level that lights this LED, or leaves it dark. Polarity lives here and
// nowhere else, so no caller has to remember which way any pin is wired.
uint8_t levelFor(const StateLed &led, bool lit) {
  return (lit != led.activeLow) ? HIGH : LOW;
}

void show(uint8_t state) {
  if (state >= STATE_COUNT) return;

  // Every LED is driven on every update, not just the one that changed. It
  // costs four writes and it means the board cannot end up showing two states
  // at once after a missed message.
  for (uint8_t i = 0; i < STATE_COUNT; i++) {
    digitalWrite(ledForState[i].pin, levelFor(ledForState[i], i == state));
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
    pinMode(ledForState[i].pin, OUTPUT);
    // Dark, whichever way this one is wired. Writing LOW blindly would light
    // the active-low LED the instant the pin became an output.
    digitalWrite(ledForState[i].pin, levelFor(ledForState[i], false));
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
