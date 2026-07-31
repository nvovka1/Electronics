#pragma once

#include <Arduino.h>

#include "GpsDiagnostics.h"
#include "GpsFix.h"

// Prints a periodic one-line status summary to the USB serial port.
//
// Deliberately separate from the raw NMEA echo (which GpsReader does): the
// echo shows what the module said, this shows what the parser made of it.
// Seeing both side by side is what distinguishes "the module is lying" from
// "we are misreading it".
class SerialLogger {
 public:
  SerialLogger(Print &output, uint32_t intervalMs);

  void begin();

  bool isDue(uint32_t nowMs) const;
  void log(uint32_t nowMs, const GpsFix &fix, const GpsDiagnostics &diagnostics);

 private:
  Print &_output;
  uint32_t _intervalMs;
  uint32_t _lastLogAtMs;
  bool _hasLogged;
};
