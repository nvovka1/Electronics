#pragma once

#include <Arduino.h>

#ifndef NODE_NAME
  #define NODE_NAME "?"
#endif

// --- On-board LoRa pins (board macros; fallbacks for LILYGO LoRa32) ---
#ifndef LORA_SCK
  #define LORA_SCK  5
  #define LORA_MISO 19
  #define LORA_MOSI 27
  #define LORA_CS   18
  #define LORA_RST  23
  #define LORA_IRQ  26
#endif
constexpr long LORA_FREQ = 868E6;   // must match both boards + your hardware

// --- On-board OLED pins ---
#ifndef OLED_SDA
  #define OLED_SDA 21
#endif
#ifndef OLED_SCL
  #define OLED_SCL 22
#endif
constexpr int8_t OLED_RESET_PIN = -1;   // this revision has no OLED reset line

// --- I/O pins ---
constexpr uint8_t LED_PIN    = 25;  // on-board LED (sidetone)
constexpr uint8_t BUZZER_PIN = 13;  // external active buzzer (- to GND)
constexpr uint8_t KEY_PIN    = 0;   // Morse key button (pressed = LOW)

// --- Battery sense ---
// GPIO35 (ADC1_CH7) sits on a 100k/100k divider from VBAT on the LoRa32 v2.1.
// ADC1 is used deliberately: ADC2 is unavailable whenever WiFi is active.
constexpr uint8_t VBAT_PIN = 35;
constexpr uint16_t VBAT_DIVIDER_Q10 = 2048;  // x2.000 in Q10, the nominal divider
