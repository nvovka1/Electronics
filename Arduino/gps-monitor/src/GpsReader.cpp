#include "GpsReader.h"

GpsReader::GpsReader(Stream &stream) : _stream(stream), _rawSink(nullptr) {}

void GpsReader::echoRawTo(Print *sink) { _rawSink = sink; }

void GpsReader::poll() {
  while (_stream.available() > 0) {
    const char received = static_cast<char>(_stream.read());

    if (_rawSink != nullptr) {
      _rawSink->write(received);
    }

    _gps.encode(received);
  }
}

GpsFix GpsReader::currentFix() {
  GpsFix fix;

  fix.isReceivingData = _gps.charsProcessed() > MinimumCharsForData;
  fix.hasFix = _gps.location.isValid() && _gps.location.age() < FixTimeoutMs;
  fix.latitude = fix.hasFix ? _gps.location.lat() : 0.0;
  fix.longitude = fix.hasFix ? _gps.location.lng() : 0.0;
  fix.satellites =
      _gps.satellites.isValid() ? static_cast<uint8_t>(_gps.satellites.value()) : 0;

  return fix;
}

GpsDiagnostics GpsReader::diagnostics() {
  GpsDiagnostics diagnostics;

  diagnostics.charsProcessed = _gps.charsProcessed();
  diagnostics.sentencesWithFix = _gps.sentencesWithFix();
  diagnostics.failedChecksums = _gps.failedChecksum();
  diagnostics.satellitesInSolution =
      _gps.satellites.isValid() ? _gps.satellites.value() : 0;

  // TinyGPSPlus stores HDOP in hundredths.
  diagnostics.hdop = _gps.hdop.isValid() ? _gps.hdop.value() / 100.0 : 0.0;
  diagnostics.locationAgeMs =
      _gps.location.isValid() ? _gps.location.age() : 0;

  return diagnostics;
}
