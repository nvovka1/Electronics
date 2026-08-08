#include "battery.h"

#include "pins.h"

namespace {

// The T3 v1.6.1 senses the cell through a 1:2 divider, so the pin sees half the
// battery voltage.
constexpr float DividerRatio = 2.0f;

constexpr float EmptyVolts = 3.30f;
constexpr float FullVolts  = 4.20f;

// Above this the board is almost certainly running from USB, not a cell.
constexpr float UsbVolts = 4.35f;

constexpr uint32_t SampleIntervalMs = 1000;
constexpr uint8_t OversampleReads   = 16;

// Exponential smoothing. The ADC on this pin is noisy and a jittering percentage
// in the status bar is worse than a slightly stale one.
constexpr float SmoothingFactor = 0.2f;

float smoothedVolts = 0.0f;
uint32_t lastSampleAt = 0;

float readVolts() {
    uint32_t totalMillivolts = 0;
    for (uint8_t i = 0; i < OversampleReads; ++i) {
        // analogReadMilliVolts applies the chip's factory ADC calibration, which
        // matters here: raw counts scaled by an assumed 3.3 V reference are off
        // by enough to misreport the charge state.
        totalMillivolts += analogReadMilliVolts(BATTERY_PIN);
    }
    const float pinVolts = (totalMillivolts / static_cast<float>(OversampleReads)) / 1000.0f;
    return pinVolts * DividerRatio;
}

}  // namespace

namespace battery {

void begin() {
    analogReadResolution(12);
    smoothedVolts = readVolts();
    lastSampleAt = millis();
}

void update() {
    const uint32_t now = millis();
    if (now - lastSampleAt < SampleIntervalMs) return;
    lastSampleAt = now;

    const float sample = readVolts();
    smoothedVolts += SmoothingFactor * (sample - smoothedVolts);
}

float volts() { return smoothedVolts; }

uint8_t percent() {
    const float fraction = (smoothedVolts - EmptyVolts) / (FullVolts - EmptyVolts);
    if (fraction <= 0.0f) return 0;
    if (fraction >= 1.0f) return 100;
    return static_cast<uint8_t>(fraction * 100.0f + 0.5f);
}

bool onUsbPower() { return smoothedVolts > UsbVolts; }

}  // namespace battery
