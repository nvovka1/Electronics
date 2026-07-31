#include <Arduino.h>
#include <SoftwareSerial.h>

#include "GpsReader.h"
#include "LcdDisplay.h"
#include "SerialLogger.h"

// --- Wiring (Arduino Uno) ---------------------------------------------------
//
// LCD 1602, 4-bit parallel mode:
//   RS -> 7   E  -> 8   D4 -> 9   D5 -> 10   D6 -> 11   D7 -> 12
//   RW -> GND (never read from the LCD), V0 -> 10k pot wiper (contrast)
//   GND -> GND, VDD -> 5V, BLA -> 5V through 220R, BLK -> GND
//   D0-D3 stay unconnected: 4-bit mode uses the upper nibble only.
//
// NEO-6M on SoftwareSerial, NOT on pins 0/1:
//   GPS TX -> pin 4       GPS RX -> pin 3 (never driven; leave the wire off)
//   VCC -> 5V, GND -> GND
//
// Keeping the module off pins 0/1 leaves the hardware UART free for logging,
// which means uploads work with everything connected and the raw NMEA can be
// watched live. SoftwareSerial can drop the odd byte under load; TinyGPSPlus
// discards those sentences on checksum, so the cost is a slightly lower update
// rate, never wrong data.

constexpr uint8_t LcdRs = 7;
constexpr uint8_t LcdEnable = 8;
constexpr uint8_t LcdD4 = 9;
constexpr uint8_t LcdD5 = 10;
constexpr uint8_t LcdD6 = 11;
constexpr uint8_t LcdD7 = 12;

constexpr uint8_t GpsRxPin = 4;  // receives the module's TX
constexpr uint8_t GpsTxPin = 3;  // required by SoftwareSerial, never wired

constexpr uint32_t GpsBaudRate = 9600;    // NEO-6M factory default
constexpr uint32_t LogBaudRate = 115200;  // headroom to echo NMEA plus status

constexpr uint32_t DisplayIntervalMs = 500;
constexpr uint32_t LogIntervalMs = 2000;

// Set to false once the wiring is trusted; the status lines alone are quieter.
constexpr bool EnableRawNmeaEcho = true;

SoftwareSerial gpsSerial(GpsRxPin, GpsTxPin);
GpsReader gpsReader(gpsSerial);
LcdDisplay display(LcdRs, LcdEnable, LcdD4, LcdD5, LcdD6, LcdD7);
SerialLogger logger(Serial, LogIntervalMs);

uint32_t lastDisplayAtMs = 0;

void setup() {
  Serial.begin(LogBaudRate);
  gpsSerial.begin(GpsBaudRate);

  display.begin();
  logger.begin();

  if (EnableRawNmeaEcho) {
    gpsReader.echoRawTo(&Serial);
  }
}

void loop() {
  gpsReader.poll();

  // Unsigned subtraction, so this stays correct across the millis() rollover.
  const uint32_t nowMs = millis();
  const bool isDisplayDue = (nowMs - lastDisplayAtMs) >= DisplayIntervalMs;
  const bool isLogDue = logger.isDue(nowMs);

  if (!isDisplayDue && !isLogDue) {
    return;
  }

  const GpsFix fix = gpsReader.currentFix();

  if (isDisplayDue) {
    lastDisplayAtMs = nowMs;
    display.showFix(fix);
  }

  if (isLogDue) {
    logger.log(nowMs, fix, gpsReader.diagnostics());
  }
}
