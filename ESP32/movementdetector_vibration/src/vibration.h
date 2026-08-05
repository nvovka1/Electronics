/*
 * Non-blocking vibration pattern player for a vibration motor module.
 * -------------------------------------------------------------------
 * Buzzes out the rhythm of the Imperial March intro on a coin / ERM motor.
 * Nothing here blocks: call vibrationUpdate() once per loop() and the
 * sequencer advances itself using millis().
 *
 * Expects a vibration motor MODULE - a breakout with the driver transistor
 * and flyback diode already on it, so its input is a plain logic-level gate.
 * A bare motor must not hang off a GPIO directly.
 */

#pragma once

#include <Arduino.h>

// Claim the given pin as the motor's control output. Call once from setup().
void vibrationBegin(uint8_t pin);

// (Re)start the pattern from its first pulse. Safe to call while running.
void vibrationStart();

// Stop the motor immediately and forget the current position.
void vibrationStop();

// Advance the sequencer. Call this on every pass through loop().
void vibrationUpdate();

// True while pulses are still pending.
bool vibrationIsActive();
