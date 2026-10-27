#include "battery.h"

// LoRa32: the battery is measured through a 100k/100k divider, so GPIO 35
// sees half the battery voltage. GPIO 35 is on ADC1, which works with WiFi on.
constexpr uint8_t VBAT_PIN = 35;
constexpr uint8_t VBAT_DIVIDER = 2;
// Li-ion: ~4.2 V full, ~3.2 V empty. Checked top to bottom: the first row
// the battery reaches wins, below the last row it is 0%.
struct BatteryLevel {
  float minVolts;
  uint8_t percent;
};
constexpr BatteryLevel BATTERY_LEVELS[] = {
  {4.1f, 100},
  {3.8f, 75},
  {3.5f, 50},
  {3.2f, 25},
};

void batteryBegin()
{
    analogSetPinAttenuation(VBAT_PIN, ADC_11db);  // measures up to ~3.1 V at the pin
    pinMode(VBAT_PIN, INPUT);
}

uint16_t getBatteryMilliVolts() {
  uint32_t total = 0;
  for (uint8_t i = 0; i < 8; i++) {
    total += analogReadMilliVolts(VBAT_PIN);
  }
  return (uint16_t)(total / 8 * VBAT_DIVIDER);
}

uint8_t getBatteryPercent() {
  const float batteryVolts = getBatteryMilliVolts() / 1000.0f;
  for (const BatteryLevel& level : BATTERY_LEVELS) {
    if (batteryVolts >= level.minVolts) return level.percent;
  }
  return 0;
}
