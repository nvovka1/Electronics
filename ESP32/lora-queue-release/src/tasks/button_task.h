#pragma once

#include <Arduino.h>
#include "app/app_events.h"

bool buttonTaskStart(UBaseType_t priority, BaseType_t core);

// Blocks the caller until the next classified press (or the timeout).
bool buttonWaitForPress(KeyEvent& event, TickType_t timeout);
