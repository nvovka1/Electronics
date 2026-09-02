#include "stepper.h"

#include "board_pins.h"

// =============================================================================
// How the motor actually turns
// =============================================================================
//
// Inside the 28BYJ-48 are four coils spaced around a toothed iron rotor. Send
// current through a coil and it becomes an electromagnet, pulling the nearest
// rotor tooth towards itself. Energise the coils one after another around the
// circle and the rotor keeps chasing a magnetic field that is always just
// ahead of it. That chase is the rotation.
//
// Nothing decides anything on the far side of these four wires. The motor has
// no electronics in it, and the ULN2003 is just four switches - each input pin
// turns one coil on or off. The table below therefore *is* the motor control:
// walk it one way and the shaft turns clockwise, walk it the other way and it
// turns anticlockwise, stop walking and the shaft stops.
//
//     phase   IN1 IN2 IN3 IN4    where the rotor is pulled
//       0      ##   .   .   .    onto coil 1
//       1      ##  ##   .   .    midway between coils 1 and 2
//       2       .  ##   .   .    onto coil 2
//       3       .  ##  ##   .    midway between coils 2 and 3
//       4       .   .  ##   .    onto coil 3
//       5       .   .  ##  ##    midway between coils 3 and 4
//       6       .   .   .  ##    onto coil 4
//       7      ##   .   .  ##    midway between coils 4 and 1
//
// The two-coils-on rows are what makes this *half*-stepping. Pulling on two
// neighbours at once parks the rotor between them, which doubles the number of
// positions the motor can hold and makes the motion noticeably smoother than
// stepping straight from coil to coil.
//
// After phase 7 the sequence wraps back to phase 0 and the rotor has advanced
// by one tooth. Repeat 4096 times and the output shaft has turned once.
// =============================================================================

namespace {

constexpr uint8_t PhaseCount = 8;
constexpr uint8_t CoilCount = 4;

// Each row is one phase; each column is one coil. HIGH means energised.
constexpr uint8_t halfStepSequence[PhaseCount][CoilCount] = {
    {HIGH, LOW,  LOW,  LOW },
    {HIGH, HIGH, LOW,  LOW },
    {LOW,  HIGH, LOW,  LOW },
    {LOW,  HIGH, HIGH, LOW },
    {LOW,  LOW,  HIGH, LOW },
    {LOW,  LOW,  HIGH, HIGH},
    {LOW,  LOW,  LOW,  HIGH},
    {HIGH, LOW,  LOW,  HIGH},
};

constexpr uint8_t coilPins[CoilCount] = {PIN_IN1, PIN_IN2, PIN_IN3, PIN_IN4};

// Where in the eight-phase cycle the rotor was left. Carrying it across calls
// matters: restarting from phase 0 would jerk the rotor back to wherever phase
// 0 happens to sit.
uint8_t currentPhase = 0;

}  // namespace

void stepperBegin() {
    for (uint8_t coil = 0; coil < CoilCount; ++coil) {
        pinMode(coilPins[coil], OUTPUT);
    }
    stepperRelease();
}

void stepperStepOnce(MoveDirection direction) {
    // One position forward or backward around the eight-phase ring. Adding
    // PhaseCount before subtracting keeps the arithmetic positive, since these
    // are unsigned.
    if (direction == MoveDirection::Forward) {
        currentPhase = (currentPhase + 1) % PhaseCount;
    } else {
        currentPhase = (currentPhase + PhaseCount - 1) % PhaseCount;
    }

    for (uint8_t coil = 0; coil < CoilCount; ++coil) {
        digitalWrite(coilPins[coil], halfStepSequence[currentPhase][coil]);
    }
}

void stepperRelease() {
    for (uint8_t coil = 0; coil < CoilCount; ++coil) {
        digitalWrite(coilPins[coil], LOW);
    }
}
