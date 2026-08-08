// Every pin this project touches, in one place.
//
// Values are guarded with #ifndef so the PlatformIO board definition wins when
// it provides its own macros. The fallbacks are the LILYGO T3 v1.6.1 wiring
// (TTGO LoRa32 V2.1-1.6.1), verified against the board schematic.

#pragma once

#include <Arduino.h>

// --- On-board LoRa (SX1276) ---
#ifndef LORA_SCK
  #define LORA_SCK  5
  #define LORA_MISO 19
  #define LORA_MOSI 27
  #define LORA_CS   18
  #define LORA_RST  23
  #define LORA_IRQ  26   // DIO0
#endif

// --- On-board OLED (SSD1306 128x64, I2C) ---
#ifndef OLED_SDA
  #define OLED_SDA 21
#endif
#ifndef OLED_SCL
  #define OLED_SCL 22
#endif
#ifndef OLED_RST
  #define OLED_RST 16
#endif

// --- On-board odds and ends ---
constexpr uint8_t LED_PIN     = 25;  // green LED next to the display
constexpr uint8_t BUTTON_PIN  = 0;   // BOOT button, pressed = LOW, has a pull-up
constexpr uint8_t BATTERY_PIN = 35;  // 18650 sense, behind a 1:2 divider

// --- External joystick (KY-023 or similar) ---
//
// GPIO 34/36/39 are the only free pins left on this board and all three are
// INPUT-ONLY with no internal pull-up. That is fine for the two analog axes,
// which is why the joystick's SW pin is left unconnected and "click" comes from
// the BOOT button instead — SW on GPIO 34 would need an external 10k pull-up.
//
// Power the joystick from 3V3, NOT 5V: the ESP32 ADC tops out at 3.3 V.
constexpr uint8_t JOYSTICK_X_PIN = 36;  // ADC1_CH0
constexpr uint8_t JOYSTICK_Y_PIN = 39;  // ADC1_CH3
