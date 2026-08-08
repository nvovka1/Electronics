// 18650 battery sense on the T3's built-in divider.

#pragma once

#include <Arduino.h>

namespace battery {

void begin();

// Call regularly; cheap, and internally rate-limited.
void update();

float volts();

// 0-100, clamped. A Li-ion cell is treated as empty at 3.3 V and full at 4.2 V.
// Crude, because a real curve needs a load model, but honest enough to tell you
// when to go home.
uint8_t percent();

// True when the reading is high enough to be USB power rather than a cell — the
// divider reads the same node either way, so this is a guess, not a measurement.
bool onUsbPower();

}  // namespace battery
