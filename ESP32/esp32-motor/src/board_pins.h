#pragma once

#include <Arduino.h>

// --- ULN2003 driver inputs ---------------------------------------------------
// The 28BYJ-48 is a unipolar motor with no step/direction logic anywhere: the
// coil pattern is produced here and shifted out on these four pins, one per
// coil. IN1..IN4 on the board map to the four LEDs in the same order.
constexpr uint8_t PIN_IN1 = 18;
constexpr uint8_t PIN_IN2 = 19;
constexpr uint8_t PIN_IN3 = 21;
constexpr uint8_t PIN_IN4 = 22;

// --- Joystick X axis ---------------------------------------------------------
// Analog input, so it must be an ADC1 pin (GPIO 32..39). GPIO 34 is input-only,
// which is exactly what an analog input wants.
//
// Power the joystick from 3V3, never 5V: its axes are potentiometers wired
// across whatever it is fed, and the ESP32's ADC inputs are 3.3 V maximum.
constexpr uint8_t PIN_JOYSTICK_X = 34;

// --- On-board LED (esp32dev / WROOM-32), lit while the motor is turning ------
constexpr uint8_t PIN_LED = 2;
