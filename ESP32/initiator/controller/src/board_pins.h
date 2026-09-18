#pragma once

#include <Arduino.h>

// LILYGO/TTGO LoRa32 v2.1 ("T3_V1.6"), the same board as the explosion node.
// The radio and OLED are on the board; the fallbacks below are here so a build
// against a different variant still says what it expected.

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

// This revision has no OLED reset line. Do NOT put GPIO 16 here: on these
// PICO-D4 boards it is the embedded flash chip select, and driving it hangs the
// board in a silent watchdog reboot loop.
constexpr int8_t OledResetPin = -1;

// --- the three buttons ----------------------------------------------------
// One job each. No long-press, no double-press, no chords: a control that means
// two things is a control that eventually does the wrong one.
//
// All three are INPUT_PULLUP and wired to GND, so pressed reads LOW.

// The on-board PRG button. Advances INIT -> ARM -> FIRE.
constexpr uint8_t ButtonSequencePin = 0;

// Sends SAFE, from any state, always. Nothing else is on this pin.
//
// On 13 rather than 36 deliberately: 13 has a working internal pull-up and 36
// does not, so this is the one of the two that cannot be made unreliable by a
// missing resistor. SAFE is the button that has to work.
constexpr uint8_t ButtonSafePin = 13;

// Cycles which node is being commanded.
//
// On 15, which has a working internal pull-up, so this button needs no external
// resistor - unlike the other two candidates on this board.
//
// IT WAS ON GPIO 36, AND THAT COST HOURS. GPIO 34-39 read inputs but have NO
// internal pull-up, so INPUT_PULLUP is silently ignored and the pin floats.
// Floating, it invented presses, quietly walked the target from node 1 to node
// 4 and wrote that to NVS. Every command then went to a node that does not
// exist; the real node heard each one, saw it was addressed elsewhere and
// stayed silent, exactly as designed. From the controller it looked like a dead
// radio link, and it survived reflashing because the wrong target was in flash,
// not in the image.
//
// GPIO 15 is a strapping pin, but a harmless one here: held LOW at reset it
// only silences the ROM boot log. A button is open at rest, so that happens
// only if somebody is holding TARGET while the board starts.
constexpr uint8_t ButtonTargetPin = 15;

// GPIO 34-39 are input-only and have no internal pull-up. A button on one of
// them is fine, but only with its own resistor.
constexpr bool pinHasInternalPullup(uint8_t pin) { return pin < 34; }

// GPIO 0 is a strapping pin: held LOW at reset it enters the bootloader instead
// of running. That is what the button is for, so it is not a problem - but it
// does mean holding SEQ while power-cycling gives you a board that looks dead
// and is actually waiting to be flashed.

// --- battery sense --------------------------------------------------------
// ADC1, because ADC2 is unavailable whenever WiFi is active. This board has no
// WiFi, but keeping the same pin as the node means one answer to "where is the
// battery read" across the project.
constexpr uint8_t BatteryPin = 35;
constexpr uint16_t BatteryDividerQ10 = 2048; // x2.000 in Q10
