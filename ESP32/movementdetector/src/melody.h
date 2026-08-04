/*
 * Non-blocking melody player for a passive buzzer / piezo.
 * ------------------------------------------------------
 * Plays the Imperial March intro on the ESP32's LEDC (hardware PWM)
 * peripheral. Nothing here blocks: call melodyUpdate() once per loop() and
 * the sequencer advances itself using millis().
 *
 * Requires a PASSIVE buzzer. An active buzzer has a fixed internal
 * oscillator and will only click or beep at one pitch.
 */

#pragma once

#include <Arduino.h>

// Claim an LEDC channel on the given pin. Call once from setup().
void melodyBegin(uint8_t buzzerPin);

// (Re)start the melody from its first note. Safe to call while playing.
void melodyStart();

// Silence the buzzer immediately and forget the current position.
void melodyStop();

// Advance the sequencer. Call this on every pass through loop().
void melodyUpdate();

// True while notes are still pending.
bool melodyIsPlaying();
