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
  bool stableDown;    // the level we have accepted
  bool candidateDown; // the level we are currently timing
  uint32_t candidateSinceMs;
  uint32_t acceptedAtMs;
};

Button buttons[] = {
    {ButtonSequencePin, ButtonSequence, false, false, 0, 0},
    {ButtonSafePin, ButtonSafe, false, false, 0, 0},
    {ButtonTargetPin, ButtonTarget, false, false, 0, 0},
};

constexpr size_t ButtonCount = sizeof(buttons) / sizeof(buttons[0]);

void poll(Button &button) {
  // INPUT_PULLUP and wired to GND, so pressed reads LOW.
  const bool isDown = digitalRead(button.pin) == LOW;
  const uint32_t now = millis();

  // The level has to HOLD for the debounce window before it is believed, rather
  // than an edge being accepted and the next few milliseconds ignored. The
  // difference matters for a pin that is not merely bouncing but floating: the
  // edge-then-blank version accepts one press per blanking period forever,
  // which fills the queue and starves the other buttons. This version accepts
  // nothing at all while the level keeps changing.
  if (isDown != button.candidateDown) {
    button.candidateDown = isDown;
    button.candidateSinceMs = now;
    return;
  }

  if ((uint32_t)(now - button.candidateSinceMs) < ButtonDebounceMs) return;
  if (isDown == button.stableDown) return;

  button.stableDown = isDown;

  // On the press, not the release. A control that acts when you let go feels
  // broken, and on the SAFE button it would be worse than that.
  if (!isDown) return;

  // Nobody presses a button ten times a second. A stream of accepted presses is
  // hardware misbehaving, not an operator, and rate-limiting it keeps one bad
  // pin from crowding the others out of the queue.
  if (button.acceptedAtMs != 0 &&
      (uint32_t)(now - button.acceptedAtMs) < ButtonMinGapMs) {
    return;
  }

  button.acceptedAtMs = now;

  // INFO, not DEBUG. A press is the whole reason this board exists, there are
  // at most a few a minute, and a field build keeps INFO - so "did the operator
  // actually press it" is answerable from any image, not just a bench one.
  LOG_INFO(TagButton, CodeButtonPressed, button.id);

  // Raised here rather than waited for, so a retry loop already in progress can
  // see it immediately instead of after the queue is next read.
  if (button.id == ButtonSafe) safePending = true;

  const ButtonEvent event = {button.id};

  // Never blocks. A full queue means the command task is busy sending, and the
  // right answer to "the operator pressed again while we were transmitting" is
  // to drop the extra press rather than to queue it up and act on it late.
  //
  // Dropped loudly, though. A press that vanishes with no trace is the worst
  // thing this firmware can do to somebody trying to work out why a button did
  // nothing - and for SAFE it is worse than that.
  if (xQueueSend(buttonQueue, &event, 0) != pdTRUE) {
    LOG_ERROR(TagButton, CodePressDropped, button.id);
  }
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
    // INPUT_PULLUP is silently ignored on GPIO 34-39, which have no internal
    // pull-up. Such a pin needs an external 10k to 3V3; see board_pins.h.
    pinMode(buttons[i].pin, INPUT_PULLUP);

    // Start from what the pin ACTUALLY reads, not from "released".
    //
    // GPIO 0 is held low for a while after reset by the board's auto-reset
    // circuit. Assuming "released" means the first poll sees a low pin, calls
    // it an edge, and reports a press nobody made - so the controller sends
    // INIT to the node every single time it boots. Seeding from the real level
    // means a pin that is already down at startup has to be released and
    // pressed again before it counts.
    const bool down = digitalRead(buttons[i].pin) == LOW;

    // A pin with no internal pull-up that is already low at boot almost
    // certainly has no external one either - nobody powers the board up holding
    // a button. Said loudly, because the alternative is what happened before:
    // the pin floats, invents presses, walks the target to a node that is not
    // there, and every symptom after that points at the radio instead.
    if (!pinHasInternalPullup(buttons[i].pin) && down) {
      LOG_ERROR(TagButton, CodeNoPullup, buttons[i].pin);
    }

    buttons[i].stableDown = down;
    buttons[i].candidateDown = down;
    buttons[i].candidateSinceMs = millis();
    buttons[i].acceptedAtMs = 0;
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
