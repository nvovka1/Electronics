#pragma once

#include <Arduino.h>

enum class MoveDirection : uint8_t {
    Forward,
    Backward,
};

// The 28BYJ-48's internal rotor takes 64 half-steps per turn, and a 64:1
// gearbox sits between it and the output shaft you can see. So the shaft turns
// once every 64 * 64 = 4096 half-steps.
constexpr uint32_t StepsPerRevolution = 4096;

// Sets the four coil pins as outputs and leaves every coil off.
void stepperBegin();

// Advances one half-step in `direction` and energises the coils for that
// phase. Returns immediately - the caller decides how long to wait before the
// next step, which is what sets the speed.
void stepperStepOnce(MoveDirection direction);

// Switches every coil off. The motor then draws nothing, and the 64:1 gearbox
// is stiff enough to hold the shaft where it was left without any current.
void stepperRelease();
