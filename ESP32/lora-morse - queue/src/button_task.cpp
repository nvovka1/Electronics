#include "button_task.h"

#include "board_pins.h"
#include "tone.h"

// --- Button timing (tune to your hand) ---
constexpr uint32_t BUTTON_POLL_MS = 5;    // how often the button task samples the pin
constexpr uint32_t DEBOUNCE_MS    = 25;   // ignore edges closer together than this
constexpr uint32_t DOUBLE_GAP_MS  = 350;  // second press within this window = double

constexpr UBaseType_t KEY_QUEUE_LENGTH = 8;

static QueueHandle_t keyEventQueue   = nullptr;
static TaskHandle_t  buttonTaskHandle = nullptr;

static void publishKeyEvent(KeyPress press) {
  KeyEvent event{press, millis()};
  // Never block on a full queue: dropping one press is far better than missing
  // the timing of the next one.
  if (xQueueSend(keyEventQueue, &event, 0) != pdTRUE)
    Serial.println("keyEventQueue full - press dropped");
}

static void buttonTask(void* /*arg*/) {
  bool     pressed       = false;   // debounced key state
  bool     ownsTone      = false;   // do we currently hold the LED + buzzer?
  uint32_t lastEdgeMs    = 0;
  uint32_t lastReleaseMs = 0;
  uint8_t  clicks        = 0;       // completed presses in the current gesture

  for (;;) {
    const uint32_t nowMs   = millis();
    const bool     rawDown = (digitalRead(KEY_PIN) == LOW);

    // --- debounced edge detection ---
    if (rawDown != pressed && (nowMs - lastEdgeMs) >= DEBOUNCE_MS) {
      pressed    = rawDown;
      lastEdgeMs = nowMs;

      if (pressed) {
        ownsTone = toneTryStart();      // live sidetone, unless a beep is playing
      } else {
        if (ownsTone) {
          toneStop();
          ownsTone = false;
        }
        // A completed press. A second one inside the window = double press.
        lastReleaseMs = nowMs;
        if (++clicks >= 2) {
          publishKeyEvent(KeyPress::Double);
          clicks = 0;
        }
      }
    }

    if (clicks == 1 && !pressed && (nowMs - lastReleaseMs) >= DOUBLE_GAP_MS) {
      publishKeyEvent(KeyPress::Single);
      clicks = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
  }
}

bool buttonTaskStart(UBaseType_t priority, BaseType_t core) {
  pinMode(KEY_PIN, INPUT_PULLUP);

  keyEventQueue = xQueueCreate(KEY_QUEUE_LENGTH, sizeof(KeyEvent));
  if (!keyEventQueue) return false;

  return xTaskCreatePinnedToCore(buttonTask, "button", 2560, nullptr, priority,
                                 &buttonTaskHandle, core) == pdPASS;
}

bool buttonWaitForPress(KeyEvent& event, TickType_t timeout) {
  if (!keyEventQueue) return false;
  return xQueueReceive(keyEventQueue, &event, timeout) == pdTRUE;
}
