#pragma once

#include <Arduino.h>

// LILYGO/TTGO LoRa32 v2.1 ("T3_V1.6"). The radio and OLED pins come from the
// board definition; the fallbacks are here so a build against a slightly
// different variant still says what it expected.

// --- on-board LoRa (SX1276) ----------------------------------------------
#ifndef LORA_SCK
  #define LORA_SCK  5
  #define LORA_MISO 19
  #define LORA_MOSI 27
  #define LORA_CS   18
  #define LORA_RST  23
  #define LORA_IRQ  26
#endif

// --- on-board OLED (SSD1306) ---------------------------------------------
#ifndef OLED_SDA
  #define OLED_SDA 21
#endif
#ifndef OLED_SCL
  #define OLED_SCL 22
#endif

// This revision has no OLED reset line, and -1 is how the driver is told so.
//
// Do NOT put GPIO 16 here. On these PICO-D4 boards GPIO 16 is the embedded
// flash chip select: driving it hangs the board in a silent watchdog reboot
// loop with nothing on the serial line to say why.
constexpr int8_t OledResetPin = -1;

// --- the four state LEDs --------------------------------------------------
// Exactly one is lit at any time, and it is always the current state. The
// colours match the badges on the dashboard on purpose: the person at the board
// and the person at the screen should describe what they see the same way.
//
// Anode to the pin through a resistor, cathode to GND.
constexpr uint8_t LedSafePin = 32;  // blue
constexpr uint8_t LedInitPin = 33;  // green
constexpr uint8_t LedArmedPin = 2;  // yellow
constexpr uint8_t LedFirePin = 14;  // red

// GPIO 2 is a strapping pin: it must not be held HIGH at boot. An LED to GND is
// the safe direction - it presents no pull-up, and the pin is an input until
// setup() drives it.
//
// GPIO 2 and 14 are also routed to the unused microSD socket. Nothing here uses
// the card, and GPIO 13 is already treated as free in the neighbouring project
// for the same reason.

// --- battery sense --------------------------------------------------------
// GPIO 35 (ADC1_CH7) sits on a 100k/100k divider from VBAT. ADC1 deliberately:
// ADC2 is unavailable whenever WiFi is active, and this node has WiFi on.
constexpr uint8_t BatteryPin = 35;
constexpr uint16_t BatteryDividerQ10 = 2048; // x2.000 in Q10, the nominal divider
