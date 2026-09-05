#include "tasks/button_task.h"

#include "core/log.h"
#include "hal/board_pins.h"
#include "tasks/tone.h"

// --- Button timing (tune to your hand) ---
constexpr uint32_t BUTTON_POLL_MS = 5;    // how often the button task samples the pin
constexpr uint32_t DEBOUNCE_MS    = 25;   // ignore edges closer together than this
constexpr uint32_t DOUBLE_GAP_MS  = 350;  // second press within this window = double
// Hold this long and the screen switches to the identity page. It is the only
// way to read a node's serial and firmware version in the field, where there
// is no laptop to type `screen info` into.
constexpr uint32_t LONG_PRESS_MS  = 2000;

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
  bool     longFired      = false;   // this hold already produced a Long event

  for (;;) {
    const uint32_t nowMs   = millis();
    const bool     rawDown = (digitalRead(KEY_PIN) == LOW);

    // --- debounced edge detection ---
    if (rawDown != pressed && (nowMs - lastEdgeMs) >= DEBOUNCE_MS) {
      pressed    = rawDown;
      lastEdgeMs = nowMs;

      if (pressed) {
        if (clicks == 0) gestureStartMs = nowMs;   // start of a new gesture
        longFired = false;
        ownsTone = toneTryStart();      // live sidetone, unless a beep is playing
      } else {
        if (ownsTone) {
          toneStop();
          ownsTone = false;
        }
        // A hold that already fired is not also a dot: releasing it must not
        // key the radio.
        if (longFired) {
          longFired = false;
          clicks = 0;
          lastReleaseMs = nowMs;
          continue;
        }

        // A completed press. A second one inside the window = double press.
        lastReleaseMs = nowMs;
        if (++clicks >= 2) {
          publishKeyEvent(KeyPress::Double, gestureStartMs);
          clicks = 0;
        }
      }
    }

    // Fires while the key is still down, so the operator sees the screen
    // change and knows to let go.
    if (pressed && !longFired && (nowMs - lastEdgeMs) >= LONG_PRESS_MS) {
      longFired = true;
      if (ownsTone) {
        toneStop();
        ownsTone = false;
      }
      publishKeyEvent(KeyPress::Long, gestureStartMs);
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
