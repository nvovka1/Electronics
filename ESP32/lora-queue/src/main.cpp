// Part 1 - two tasks, two cores, one queue:
//
//   core 0 : button task - watches the BOOT button and decides the interval
//   core 1 : LED task    - blinks the on-board LED at that interval
//
// The button task never writes the LED task's interval: it sends a
// BlinkCommand through blinkCommandQueue and the LED task applies it.
//
// Part 2 - the same firmware also carries a mutex lab that is broken on
// purpose. A TRIPLE click arms it (see mutex_lab.h). The blink tasks above are
// left running deliberately: the LED is the liveness indicator that tells you
// how far the resulting hang spread.

#include <Arduino.h>

#include "blink_command.h"
#include "board_pins.h"
#include "button_task.h"
#include "led_task.h"
#include "mutex_lab.h"

constexpr UBaseType_t BLINK_QUEUE_LENGTH = 4;

constexpr BaseType_t BUTTON_CORE = 0;
constexpr BaseType_t LED_CORE    = 1;

static QueueHandle_t blinkCommandQueue = nullptr;

static void fail(const char* what) {
  for (;;) {
    Serial.printf("FATAL: %s failed\n", what);
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nBlink-interval demo  (LED on GPIO %u, button on GPIO %u)\n",
                LED_PIN, BUTTON_PIN);
  Serial.println("single press = next interval, double press = one step back");
  Serial.printf("triple press = arm the mutex lab: %s\n", mutexLabName());

  blinkCommandQueue = xQueueCreate(BLINK_QUEUE_LENGTH, sizeof(BlinkCommand));
  if (!blinkCommandQueue) fail("xQueueCreate");

  if (!ledTaskStart(blinkCommandQueue, 2, LED_CORE)) fail("ledTaskStart");
  if (!buttonTaskStart(blinkCommandQueue, 3, BUTTON_CORE)) fail("buttonTaskStart");

  Serial.printf("[setup  core %d] button task -> core %d, LED task -> core %d\n",
                xPortGetCoreID(), (int)BUTTON_CORE, (int)LED_CORE);
}

void loop() {
  // Both tasks do the work; nothing left for the Arduino loop.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
