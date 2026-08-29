#pragma once

#include <Arduino.h>

// The one and only thing the button task tells the LED task. It travels through
// a FreeRTOS queue - neither task touches the other's variables.
struct BlinkCommand {
  uint32_t intervalMs;   // time the LED stays in one state before it toggles
  uint8_t  stepIndex;    // position in the interval table, for logging only
};
