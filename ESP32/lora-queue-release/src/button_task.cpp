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

static void publishKeyEvent(KeyPress press, uint32_t pressedAtMs) {
  const KeyEvent event{press, EventStamp{pressedAtMs, millis()}};

  const bool queued = (xQueueSend(keyEventQueue, &event, 0) == pdTRUE);

  Serial.printf("[key]    %-6s  press@%lu ms  detect %lu ms%s\n",
                press == KeyPress::Double ? "double" : "single",
                (unsigned long)pressedAtMs,
                (unsigned long)(event.stamp.keyedAtMs - pressedAtMs),
                queued ? "" : "  [keyEventQueue FULL - dropped]");
}

static void buttonTask(void* /*arg*/) {
  bool     pressed        = false;   // debounced key state
  bool     ownsTone       = false;   // do we currently hold the LED + buzzer?
  uint32_t lastEdgeMs     = 0;
  uint32_t lastReleaseMs  = 0;
  uint32_t gestureStartMs = 0;       // first press edge of the current gesture
  uint8_t  clicks         = 0;       // completed presses in the current gesture

  for (;;) {
    const uint32_t nowMs   = millis();
    const bool     rawDown = (digitalRead(KEY_PIN) == LOW);

    // --- debounced edge detection ---
    if (rawDown != pressed && (nowMs - lastEdgeMs) >= DEBOUNCE_MS) {
      pressed    = rawDown;
      lastEdgeMs = nowMs;

      if (pressed) {
        if (clicks == 0) gestureStartMs = nowMs;   // start of a new gesture
        ownsTone = toneTryStart();      // live sidetone, unless a beep is playing
      } else {
        if (ownsTone) {
          toneStop();
          ownsTone = false;
        }
        // A completed press. A second one inside the window = double press.
        lastReleaseMs = nowMs;
        if (++clicks >= 2) {
          publishKeyEvent(KeyPress::Double, gestureStartMs);
          clicks = 0;
        }
      }
    }

    if (clicks == 1 && !pressed && (nowMs - lastReleaseMs) >= DOUBLE_GAP_MS) {
      publishKeyEvent(KeyPress::Single, gestureStartMs);
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
