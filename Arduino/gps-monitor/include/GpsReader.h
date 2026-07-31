#pragma once

#include <Arduino.h>
#include <TinyGPS++.h>

#include "GpsDiagnostics.h"
#include "GpsFix.h"

// Turns the NMEA sentence stream coming out of a NEO-6M into a GpsFix.
//
// Receive-only by design: a 5 V TX line would over-drive the module's 3.3 V RX
// pin, so this class never writes to the stream it was given.
class GpsReader {
 public:
  explicit GpsReader(Stream &stream);

  // Mirrors every received byte to sink, so the raw sentences can be watched
  // on the serial monitor. Pass nullptr to stop echoing.
  void echoRawTo(Print *sink);

  // Feeds every byte buffered so far into the parser. Call this often --
  // a SoftwareSerial buffer holds 64 bytes and a 1 Hz NMEA burst is longer
  // than that.
  void poll();

  // Not const: TinyGPSPlus clears its "updated" flags when values are read.
  GpsFix currentFix();
  GpsDiagnostics diagnostics();

 private:
  // A position older than this is treated as no fix rather than shown as if
  // it were current.
  static constexpr uint32_t FixTimeoutMs = 5000;

  // Enough characters to prove the serial line works; roughly one sentence.
  static constexpr uint32_t MinimumCharsForData = 10;

  Stream &_stream;
  Print *_rawSink;
  TinyGPSPlus _gps;
};
