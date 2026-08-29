#include "led_task.h"

#include "blink_command.h"
#include "board_pins.h"

static QueueHandle_t blinkCommandQueue = nullptr;
static TaskHandle_t  ledTaskHandle     = nullptr;

static void ledTask(void* /*arg*/) {
  uint32_t intervalMs = 0;       // 0 = nothing received yet, so do not blink
  bool     ledOn      = false;

  digitalWrite(LED_PIN, LOW);

  for (;;) {
    const TickType_t wait = (intervalMs == 0) ? portMAX_DELAY : pdMS_TO_TICKS(intervalMs);

    BlinkCommand command;
    if (xQueueReceive(blinkCommandQueue, &command, wait) == pdTRUE) {
      intervalMs = command.intervalMs;

      // Restart the cycle lit, so a change is visible immediately.
      ledOn = true;
      digitalWrite(LED_PIN, HIGH);

      Serial.printf("[led    core %d] interval := %lu ms (step %u)\n",
                    xPortGetCoreID(), (unsigned long)intervalMs, command.stepIndex);
    } else {
      ledOn = !ledOn;
      digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
    }
  }
}

bool ledTaskStart(QueueHandle_t commandQueue, UBaseType_t priority, BaseType_t core) {
  if (!commandQueue) return false;
  blinkCommandQueue = commandQueue;

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  return xTaskCreatePinnedToCore(ledTask, "led", 2560, nullptr, priority,
                                 &ledTaskHandle, core) == pdPASS;
}
