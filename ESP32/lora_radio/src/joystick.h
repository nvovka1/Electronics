// Analog joystick + BOOT button, reduced to a stream of discrete events.
//
// Callers never see ADC counts. They call poll() once per loop and get back one
// event, which makes the UI code a plain state machine instead of a pile of
// threshold comparisons.

#pragma once

#include <Arduino.h>

enum class JoystickEvent : uint8_t {
    None,
    Up,
    Down,
    Left,
    Right,
    Click,      // BOOT button, short press (fires on release)
    LongClick,  // BOOT button, held past the long-press threshold
};

namespace joystick {

// Samples the centre position, so the stick must be left alone at boot.
void begin();

// Call once per loop(). Returns at most one event per call.
JoystickEvent poll();

// --- Diagnostics, for the settings page and the joystick-check build ---

int rawX();
int rawY();
int centreX();
int centreY();
bool buttonDown();

// False when the centre reading at boot was implausible — the axes are
// input-only pins with no pull-up, so an unpowered or unwired stick floats and
// reads garbage. Better to say so than to emit phantom input forever.
bool detected();

const char* eventName(JoystickEvent event);

}  // namespace joystick
