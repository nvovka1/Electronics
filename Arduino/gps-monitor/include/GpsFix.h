#pragma once

#include <stdint.h>

// A snapshot of what the GPS module currently knows.
//
// This is the only thing that crosses the boundary between GpsReader and
// LcdDisplay, so the display code never has to know that NMEA exists.
struct GpsFix {
  // True when the module is sending us bytes at all. False means the wiring
  // or the baud rate is wrong, not that the sky is cloudy.
  bool isReceivingData;

  // True when a position has been decoded and is still fresh.
  bool hasFix;

  // Decimal degrees; only meaningful while hasFix is true.
  double latitude;
  double longitude;

  // Satellites used in the last solution, 0 when unknown.
  uint8_t satellites;
};
