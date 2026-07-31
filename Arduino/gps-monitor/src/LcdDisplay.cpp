#include "LcdDisplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LcdDisplay::LcdDisplay(uint8_t rs, uint8_t enable, uint8_t d4, uint8_t d5,
                       uint8_t d6, uint8_t d7)
    : _lcd(rs, enable, d4, d5, d6, d7) {
  memset(_renderedLines, 0, sizeof(_renderedLines));
}

void LcdDisplay::begin() {
  _lcd.begin(Columns, Rows);
  writeLine(0, "GPS monitor");
  writeLine(1, "Starting...");
}

void LcdDisplay::showFix(const GpsFix &fix) {
  char firstLine[Columns + 1];
  char secondLine[Columns + 1];

  if (!fix.isReceivingData) {
    // Nothing at all on the serial line: this is a wiring or baud problem,
    // so point at the wire that is almost always the culprit.
    strcpy(firstLine, "No GPS data");
    strcpy(secondLine, "GPS TX -> pin 0");
  } else if (!fix.hasFix) {
    strcpy(firstLine, "Acquiring fix...");
    snprintf(secondLine, sizeof(secondLine), "Satellites: %2u",
             static_cast<unsigned int>(fix.satellites));
  } else {
    formatCoordinate("Lat:", fix.latitude, firstLine);
    formatCoordinate("Lon:", fix.longitude, secondLine);
  }

  writeLine(0, firstLine);
  writeLine(1, secondLine);
}

void LcdDisplay::formatCoordinate(const char *label, double value, char *out) {
  // Six decimals is about 0.11 m at the equator -- far finer than the module
  // resolves, but it keeps the column width fixed so digits never jump around.
  char number[Columns];
  dtostrf(value, Columns - 4, 6, number);
  snprintf(out, Columns + 1, "%s%s", label, number);
}

void LcdDisplay::writeLine(uint8_t row, const char *text) {
  char padded[Columns + 1];

  uint8_t column = 0;
  while (column < Columns && text[column] != '\0') {
    padded[column] = text[column];
    column++;
  }
  while (column < Columns) {
    padded[column++] = ' ';
  }
  padded[Columns] = '\0';

  if (strcmp(padded, _renderedLines[row]) == 0) {
    return;
  }

  strcpy(_renderedLines[row], padded);
  _lcd.setCursor(0, row);
  _lcd.print(padded);
}
