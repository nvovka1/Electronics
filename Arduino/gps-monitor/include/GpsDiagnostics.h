#pragma once

#include <stdint.h>

// Health counters for the GPS link, for logging rather than display.
//
// Kept separate from GpsFix on purpose: GpsFix answers "what should the screen
// say", this answers "is the link working and how well".
struct GpsDiagnostics {
  // Bytes fed to the parser since boot. Stuck at 0 means the RX wire is wrong.
  uint32_t charsProcessed;

  // Sentences that both passed checksum and claimed a fix.
  uint32_t sentencesWithFix;

  // Corrupt sentences. A steadily climbing count means noise or a baud
  // mismatch; a few during startup are normal.
  uint32_t failedChecksums;

  // Satellites used in the solution, as reported in GGA. This is not
  // satellites in view -- it stays 0 until a fix is achieved.
  uint32_t satellitesInSolution;

  // Horizontal dilution of precision. Below 2 is good, above 5 is a weak
  // solution. 99.99 is the module's way of saying "no idea".
  double hdop;

  // Milliseconds since the last position was committed.
  uint32_t locationAgeMs;
};
