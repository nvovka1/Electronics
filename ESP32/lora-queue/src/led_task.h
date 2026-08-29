#pragma once

#include <Arduino.h>

// Starts the LED task on `core`. It blinks the on-board LED and takes its
// interval from `commandQueue` - it owns no other input. Until the first
// command arrives the LED stays off.
bool ledTaskStart(QueueHandle_t commandQueue, UBaseType_t priority, BaseType_t core);
