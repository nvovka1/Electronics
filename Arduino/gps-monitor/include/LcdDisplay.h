#pragma once

#include <Arduino.h>
#include <LiquidCrystal.h>

#include "GpsFix.h"

// Renders a GpsFix onto a 16x2 character LCD wired in 4-bit parallel mode.
//
// Every write is a full-width, space-padded line written over the previous
// one, so there is never a call to clear() -- that call is slow and makes the
// display visibly flicker once a second.
class LcdDisplay {
 public:
  LcdDisplay(uint8_t rs, uint8_t enable, uint8_t d4, uint8_t d5, uint8_t d6,
             uint8_t d7);

  void begin();

  // Redraws only the lines whose text actually changed.
  void showFix(const GpsFix &fix);

 private:
  static constexpr uint8_t Columns = 16;
  static constexpr uint8_t Rows = 2;

  void writeLine(uint8_t row, const char *text);

  // Formats a coordinate as "<label><right-aligned value>", exactly 16 wide.
  static void formatCoordinate(const char *label, double value, char *out);

  LiquidCrystal _lcd;
  char _renderedLines[Rows][Columns + 1];
};
