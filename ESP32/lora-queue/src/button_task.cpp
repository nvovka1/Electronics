#include "button_task.h"

#include "blink_command.h"
#include "board_pins.h"
#include "mutex_lab.h"

// --- The interval cycle, in milliseconds ---
static constexpr uint32_t BLINK_INTERVALS_MS[] = {250, 500, 1000, 2000};
static constexpr uint8_t  INTERVAL_COUNT = sizeof(BLINK_INTERVALS_MS) / sizeof(BLINK_INTERVALS_MS[0]);

// --- Button timing (tune to your hand) ---
constexpr uint32_t BUTTON_POLL_MS  = 5;    // how often the task samples the pin
constexpr uint32_t DEBOUNCE_MS     = 25;   // ignore edges closer together than this
constexpr uint32_t GESTURE_GAP_MS  = 300;  // button up this long = the gesture is over

static QueueHandle_t blinkCommandQueue = nullptr;
static TaskHandle_t  buttonTaskHandle  = nullptr;

// Lives only inside this task: the LED task never sees it, it only ever sees
// the commands that come out of the queue.
static uint8_t currentStep = 0;

static void sendCurrentInterval(const char* reason) {
  const BlinkCommand command{BLINK_INTERVALS_MS[currentStep], currentStep};

  const bool queued = (xQueueSend(blinkCommandQueue, &command, 0) == pdTRUE);

  Serial.printf("[button core %d] %-6s -> step %u = %lu ms%s\n",
                xPortGetCoreID(), reason, command.stepIndex,
                (unsigned long)command.intervalMs,
                queued ? "" : "  [queue FULL - dropped]");
}

static void stepForward() {
  currentStep = (currentStep + 1) % INTERVAL_COUNT;
  sendCurrentInterval("single");
}

static void stepBack() {
  currentStep = (currentStep + INTERVAL_COUNT - 1) % INTERVAL_COUNT;
  sendCurrentInterval("double");
}

// A finished gesture: as many clicks as the user managed inside the window.
static void handleGesture(uint8_t clicks) {
  switch (clicks) {
    case 1:  stepForward(); break;
    case 2:  stepBack();    break;
    default:
      Serial.printf("[button core %d] triple -> arming the mutex lab\n", xPortGetCoreID());
      mutexLabStart();
      break;
  }
}

static void buttonTask(void* /*arg*/) {
  bool     pressed       = false;   // debounced button state
  uint32_t lastEdgeMs    = 0;
  uint32_t lastReleaseMs = 0;
  uint8_t  clicks        = 0;       // completed presses of the current gesture

  sendCurrentInterval("start");

  for (;;) {
    const uint32_t nowMs   = millis();
    const bool     rawDown = (digitalRead(BUTTON_PIN) == LOW);

    // --- debounced edge detection ---
    if (rawDown != pressed && (nowMs - lastEdgeMs) >= DEBOUNCE_MS) {
      pressed    = rawDown;
      lastEdgeMs = nowMs;

      // Count on release, so holding the button down is still one click.
      if (!pressed) {
        lastReleaseMs = nowMs;
        if (clicks < 250) ++clicks;
      }
    }

    // The gesture ends once the button has stayed up for the whole window;
    // only then do we know whether it was single, double or triple.
    if (clicks > 0 && !pressed && (nowMs - lastReleaseMs) >= GESTURE_GAP_MS) {
      handleGesture(clicks);
      clicks = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
  }
}

bool buttonTaskStart(QueueHandle_t commandQueue, UBaseType_t priority, BaseType_t core) {
  if (!commandQueue) return false;
  blinkCommandQueue = commandQueue;

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  return xTaskCreatePinnedToCore(buttonTask, "button", 3072, nullptr, priority,
                                 &buttonTaskHandle, core) == pdPASS;
}
