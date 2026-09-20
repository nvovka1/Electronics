#pragma once

#include <stdint.h>

// The recorder. Photographs the shared snapshot on a fixed tick and appends one
// CSV row per photograph.
//
// On a tick rather than on message arrival, deliberately. A flight controller
// that has stopped talking then produces rows whose linkAgeMs climbs, which is
// evidence; the alternative is a gap in the file, which is an absence of
// evidence and indistinguishable from the board having been switched off.

void logTaskStart();

uint16_t logFlightId();
uint32_t logRowsWritten();
uint32_t logWriteFailures();
