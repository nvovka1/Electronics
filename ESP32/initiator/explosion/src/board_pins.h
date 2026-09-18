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
constexpr uint8_t LedSafePin = 13;  // blue
constexpr uint8_t LedInitPin = 2;   // green
constexpr uint8_t LedArmedPin = 14; // yellow
constexpr uint8_t LedFirePin = 0;   // red - WIRED THE OTHER WAY ROUND, see below

// Which way each LED is wired. Three are anode -> resistor -> pin, cathode to
// GND, so the pin drives HIGH to light them. The red one is not.
constexpr bool LedSafeActiveLow = false;
constexpr bool LedInitActiveLow = false;
constexpr bool LedArmedActiveLow = false;
constexpr bool LedFireActiveLow = true;

// WHY THE RED LED IS BACKWARDS
//
// GPIO 0 is the boot strapping pin. With a LED to GND the board does not run at
// all: the on-board 10k pull-up sources about 330 uA, the LED clamps the pin
// near 1.6 V, and the ESP32 needs above 2.48 V to read high at reset. GPIO 0
// comes up LOW and the chip enters serial download mode instead of starting
// this firmware - no radio, no ACK, and a board that looks alive. Flashing
// still appears to work, because the board is already in the mode the flasher
// wants, so a successful upload does not prove the pin is free.
//
// Wired the other way - anode to 3V3, cathode through the resistor to GPIO 0 -
// the LED becomes a pull-up. The pin reads HIGH at reset, the board boots, and
// driving the pin LOW lights the LED.
//
// The cost: GPIO 0 is also the on-board PRG button, which shorts it to GND.
// Pressing PRG while this pin is driven HIGH (red off) shorts the output.
// Briefly it survives; it is still a reason not to press PRG while running.
//
// PINS THAT CANNOT DRIVE A LED, WHATEVER THE CODE SAYS
//
// GPIO 34-39 are input-only - no output driver in the silicon, so
// pinMode(OUTPUT) is a silent no-op and digitalWrite does nothing. A LED on one
// of them never lights and nothing reports a fault.
//
// GPIO 16 and 17 are the embedded flash on the PICO-D4.
// GPIO 32 and 33 are the 32.768 kHz crystal on this revision, not broken out.
//
// GPIO 2 is a strapping pin, but a LED to GND is the direction it wants at
// boot. GPIO 2, 13 and 14 are routed to the unused microSD socket, which is
// what makes them free here.

// --- battery sense --------------------------------------------------------
// GPIO 35 (ADC1_CH7) sits on a 100k/100k divider from VBAT. ADC1 deliberately:
// ADC2 is unavailable whenever WiFi is active, and this node has WiFi on.
constexpr uint8_t BatteryPin = 35;
constexpr uint16_t BatteryDividerQ10 = 2048; // x2.000 in Q10, the nominal divider
