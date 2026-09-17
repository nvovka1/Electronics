#pragma once

#include "events.h"

// The four state LEDs. Exactly one is lit at any moment and it is always the
// current state - there is no combination and no off state while running, so a
// glance at the board is never ambiguous.

void ledTaskStart();

// Safe from any task.
void ledPost(const LedRequest &request);
