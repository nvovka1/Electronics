#include "button_task.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "board_pins.h"
#include "config.h"
#include "log.h"

namespace {

QueueHandle_t buttonQueue = nullptr;

// Set the instant SAFE is pressed, cleared when the command task takes the
// event off the queue. A plain bool is enough: one writer, one reader, and a
// byte store is atomic on this core.
volatile bool safePending = false;

struct Button {
  uint8_t pin;
  uint8_t id;
  bool wasDown;
  uint32_t changedAtMs;
};

Button buttons[] = {
    {ButtonSequencePin, ButtonSequence, false, 0},
    {ButtonSafePin, ButtonSafe, false, 0},
    {ButtonTargetPin, ButtonTarget, false, 0},
};

constexpr size_t ButtonCount = sizeof(buttons) / sizeof(buttons[0]);

void poll(Button &button) {
  // INPUT_PULLUP and wired to GND, so pressed reads LOW.
  const bool isDown = digitalRead(button.pin) == LOW;
  if (isDown == button.wasDown) return;

  const uint32_t now = millis();

  // Ignore anything inside the debounce window. Contact bounce on a mechanical
  // switch is several transitions in a few milliseconds, and without this a
  // single press sends three commands.
  if ((uint32_t)(now - button.changedAtMs) < ButtonDebounceMs) return;

  button.wasDown = isDown;
  button.changedAtMs = now;

  // On the press, not the release. A control that acts when you let go feels
  // broken, and on the SAFE button it would be worse than that.
  if (!isDown) return;

  LOG_DEBUG(TagButton, CodeButtonPressed, button.id);

  // Raised here rather than waited for, so a retry loop already in progress can
  // see it immediately instead of after the queue is next read.
  if (button.id == ButtonSafe) safePending = true;

  const ButtonEvent event = {button.id};

  // Never blocks. A full queue means the command task is busy sending, and the
  // right answer to "the operator pressed again while we were transmitting" is
  // to drop the extra press rather than to queue it up and act on it late.
  xQueueSend(buttonQueue, &event, 0);
}

void buttonTask(void *) {
  for (;;) {
    for (size_t i = 0; i < ButtonCount; i++) poll(buttons[i]);
    vTaskDelay(pdMS_TO_TICKS(ButtonPollMs));
  }
}

} // namespace

void buttonTaskStart() {
  for (size_t i = 0; i < ButtonCount; i++) {
    pinMode(buttons[i].pin, INPUT_PULLUP);
    buttons[i].wasDown = false;
    buttons[i].changedAtMs = 0;
  }

  buttonQueue = xQueueCreate(ButtonQueueDepth, sizeof(ButtonEvent));

  if (buttonQueue == nullptr ||
      xTaskCreate(buttonTask, "button", ButtonTaskStack, nullptr, ButtonTaskPriority,
                  nullptr) != pdPASS) {
    LOG_ERROR(TagSys, CodeTaskStartFail, 0);
  }
}

bool buttonWait(ButtonEvent *out, uint32_t timeoutMs) {
  if (buttonQueue == nullptr || out == nullptr) return false;

  if (xQueueReceive(buttonQueue, out, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) return false;

  // Cleared as the event is handed over, not when the command completes: the
  // flag means "a press is waiting to be acted on", and from here it is.
  if (out->button == ButtonSafe) safePending = false;

  return true;
}

bool buttonSafePending() { return safePending; }
