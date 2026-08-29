#pragma once

#include <Arduino.h>

// On-board LED. Set per environment in platformio.ini (25 on LoRa32, 2 on a
// plain DevKit); the fallback keeps the header usable on its own.
#ifndef APP_LED_PIN
  #define APP_LED_PIN 2
#endif
constexpr uint8_t LED_PIN = APP_LED_PIN;

// On-board BOOT button: pressed = LOW, needs the internal pull-up.
constexpr uint8_t BUTTON_PIN = 0;
