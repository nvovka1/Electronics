#pragma once

#include <Arduino.h>

#include "frame.h"

// Twelve bytes, once every health_period_s, and the whole picture of the
// network is built out of them. It is the cheapest diagnostic there is -
// twelve bytes against a drive out - and the only one that works while the
// node is still alive.

payload_health_t healthSnapshot();

void healthPrint(Print &out);
