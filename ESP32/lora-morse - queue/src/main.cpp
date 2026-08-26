#include <Arduino.h>
#include "board_pins.h"
#include "button_task.h"
#include "radio_task.h"
#include "tone.h"
#include "ui_task.h"

// Cores: the button task shares the Arduino core with loop(); the radio and UI
// tasks run on the other one, so drawing or a beep never delays key sampling.
constexpr BaseType_t KEY_CORE   = 1;
constexpr BaseType_t WORK_CORE  = 0;

static void halt(const char* message) {
  Serial.println(message);
  uiShowFatal(message);
  for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

void setup() {
  Serial.begin(115200);

  if (!toneBegin())  halt("Tone mutex failed");
  if (!uiBegin())    Serial.println("OLED init failed.");
  if (!radioBegin()) halt("LoRa FAIL - pins/antenna");

  if (!uiTaskStart(1, WORK_CORE))     halt("UI task failed");
  if (!radioTaskStart(2, WORK_CORE))  halt("Radio task failed");
  if (!buttonTaskStart(3, KEY_CORE))  halt("Button task failed");

  uiPostBanner("1 tap=.  2 taps=-");
  Serial.printf("Node %s ready. 1 tap = dot, 2 taps = dash.\n", NODE_NAME);
}

// The dispatcher: block on the button queue, translate the press type into a
// Morse symbol, hand it to the radio task. Nothing else belongs here.
void loop() {
  KeyEvent event;
  if (!buttonWaitForPress(event, portMAX_DELAY)) return;

  const bool isDouble = (event.press == KeyPress::Double);
  const char symbol   = isDouble ? '-' : '.';
  Serial.printf("[loop] %s press @%lu ms -> '%c'\n",
                isDouble ? "double" : "single",
                (unsigned long)event.atMs, symbol);

  if (!radioSendSymbol(symbol, pdMS_TO_TICKS(50)))
    Serial.println("radioQueue full - symbol dropped");
}
