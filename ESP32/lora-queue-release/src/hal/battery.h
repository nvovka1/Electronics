#pragma once

#include <Arduino.h>

// Battery sense on the LoRa32 v2.1: a 100k/100k divider from VBAT to GPIO35,
// so the pin sees half the pack voltage.
//
// The scale factor is a property of this particular board - resistor tolerance
// and the ADC's own offset differ from unit to unit - so it lives in the
// calibration namespace in NVS, not in the config. `config reset` must never
// touch it: thresholds are a setting, a divider ratio is hardware, and it
// cannot be measured again in the field.

void batteryBegin();

// Calibrated pack voltage. Reads the ADC each call (a few hundred
// microseconds), averaged over several samples to settle the noise.
uint16_t batteryMillivolts();

// Tenths of a volt, which is the resolution the health frame carries.
uint8_t batteryDeciVolts();

// False once the ADC self-test has failed. A stuck ADC reads a plausible
// constant, so a reading that cannot be trusted must be treated as no reading
// at all rather than as a good one.
bool batteryTrusted();
void batterySetTrusted(bool trusted);

// Raw pin millivolts before the divider and the calibration are applied,
// averaged like batteryMillivolts().
uint16_t batteryRawPinMillivolts();

// One unaveraged 12-bit conversion. The ADC self-test needs this rather than
// the averaged reading: averaging eight samples and dividing is exactly what
// hides the one-LSB jitter the test is looking for, so it would report a
// healthy ADC as dead.
uint16_t batteryRawAdc();
