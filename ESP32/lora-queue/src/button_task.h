#pragma once

#include <Arduino.h>

// Starts the button task on `core`. Every recognised gesture is turned into a
// BlinkCommand and pushed into `commandQueue`:
//
//   single press -> next interval  (0.25 -> 0.5 -> 1 -> 2 -> 0.25 s)
//   double press -> one step back  (1 s -> double -> 0.5 s)
//   triple press -> arms the mutex lab (part 2) and breaks the system on purpose
//
// The first command is sent right at start-up, so the LED task learns its
// starting interval from the queue as well.
bool buttonTaskStart(QueueHandle_t commandQueue, UBaseType_t priority, BaseType_t core);
