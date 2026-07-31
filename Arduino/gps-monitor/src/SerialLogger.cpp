#include "SerialLogger.h"

SerialLogger::SerialLogger(Print &output, uint32_t intervalMs)
    : _output(output), _intervalMs(intervalMs), _lastLogAtMs(0), _hasLogged(false) {}

void SerialLogger::begin() {
  _output.println();
  _output.println(F("[gps-monitor] started. Raw NMEA echo is interleaved below."));
  _output.println(F("[gps-monitor] status format:"));
  _output.println(F("  rx     = bytes received from the module"));
  _output.println(F("  fixes  = sentences that passed checksum AND claimed a fix"));
  _output.println(F("  bad    = sentences that failed checksum"));
  _output.println(F("  sats   = satellites used in the solution (0 until locked)"));
  _output.println(F("  hdop   = horizontal dilution: <2 good, >5 weak"));
  _output.println();
}

bool SerialLogger::isDue(uint32_t nowMs) const {
  // Unsigned subtraction, so this survives the millis() rollover.
  return !_hasLogged || (nowMs - _lastLogAtMs) >= _intervalMs;
}

void SerialLogger::log(uint32_t nowMs, const GpsFix &fix,
                       const GpsDiagnostics &diagnostics) {
  _lastLogAtMs = nowMs;
  _hasLogged = true;

  // Leading newline: the raw echo may have left a partial line behind.
  _output.println();
  _output.print(F("[status "));
  _output.print(nowMs / 1000);
  _output.print(F("s] rx="));
  _output.print(diagnostics.charsProcessed);
  _output.print(F(" fixes="));
  _output.print(diagnostics.sentencesWithFix);
  _output.print(F(" bad="));
  _output.print(diagnostics.failedChecksums);
  _output.print(F(" sats="));
  _output.print(diagnostics.satellitesInSolution);
  _output.print(F(" hdop="));
  _output.print(diagnostics.hdop, 2);

  if (!fix.isReceivingData) {
    _output.println(F(" | NO DATA - check the GPS TX wire"));
    return;
  }

  if (!fix.hasFix) {
    _output.println(F(" | NO FIX - acquiring"));
    return;
  }

  _output.print(F(" | FIX lat="));
  _output.print(fix.latitude, 6);
  _output.print(F(" lon="));
  _output.print(fix.longitude, 6);
  _output.print(F(" age="));
  _output.print(diagnostics.locationAgeMs);
  _output.println(F("ms"));
}
