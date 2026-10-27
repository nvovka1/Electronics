#pragma once

#include <Arduino.h>

void batteryBegin();

uint16_t getBatteryMilliVolts();

uint8_t getBatteryPercent();