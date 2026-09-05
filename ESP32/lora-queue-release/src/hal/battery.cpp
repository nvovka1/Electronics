#include "hal/battery.h"

#include "core/calib.h"
#include "hal/board_pins.h"

static constexpr uint8_t SAMPLES = 8;
static bool s_trusted = true;

void batteryBegin() {
  // 11 dB attenuation puts the usable input range at roughly 150..2450 mV,
  // which covers half of a 3.0..4.2 V pack with room at both ends.
  analogSetPinAttenuation(VBAT_PIN, ADC_11db);
  pinMode(VBAT_PIN, INPUT);
}

uint16_t batteryRawPinMillivolts() {
  uint32_t total = 0;
  for (uint8_t i = 0; i < SAMPLES; i++) total += analogReadMilliVolts(VBAT_PIN);
  return (uint16_t)(total / SAMPLES);
}

uint16_t batteryMillivolts() {
  // pin_mv x divider x calibration, both in Q10, so the two shifts cancel into
  // one division by 2^20. Integer throughout: this is called from the write
  // gate, and a float there buys nothing.
  const uint32_t pin = batteryRawPinMillivolts();
  const uint32_t scaled =
      (pin * (uint32_t)VBAT_DIVIDER_Q10 * (uint32_t)calibVbatScaleQ10()) >> 20;
  return (uint16_t)scaled;
}

uint8_t batteryDeciVolts() {
  const uint16_t mv = batteryMillivolts();
  const uint16_t dv = (uint16_t)((mv + 50u) / 100u); // round to the nearest tenth
  return (uint8_t)(dv > 255 ? 255 : dv);
}

bool batteryTrusted() { return s_trusted; }

void batterySetTrusted(bool trusted) { s_trusted = trusted; }
