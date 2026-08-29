#pragma once

#include <Arduino.h>

struct BlinkCommand {
  uint32_t intervalMs;
  uint8_t  stepIndex;
};
