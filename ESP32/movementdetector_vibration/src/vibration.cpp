#include "vibration.h"

namespace {

struct Pulse {
  bool     vibrates;    // false means a still slot (the rest in the phrase)
  uint16_t durationMs;  // full slot length, including the gap after the pulse
};

// "The Imperial March" (Darth Vader's theme), opening phrase. The pitches are
// gone - a motor only has on and off - but the rhythm is what you feel anyway.
constexpr Pulse PATTERN[] = {
  {true, 500}, {true, 500}, {true, 500},
  {true, 350}, {true, 150},
  {true, 500}, {true, 350}, {true, 150}, {true, 650},
  {false, 500},
  {true, 500}, {true, 500}, {true, 500},
  {true, 350}, {true, 150},
  {true, 500}, {true, 350}, {true, 150}, {true, 650},
};

constexpr size_t PATTERN_LENGTH = sizeof(PATTERN) / sizeof(PATTERN[0]);

// Every slot ends in a still gap so that consecutive pulses stay separate.
// An ERM motor keeps spinning for a few tens of ms after the current is cut,
// so the gap needs a floor: a purely proportional gap would leave the short
// 150 ms slots with ~45 ms of "silence" that nobody would ever feel, and the
// triplet would smear into one long buzz.
constexpr uint8_t  GAP_PERCENT = 30;
constexpr uint16_t MIN_GAP_MS  = 60;

uint8_t       motorPin       = 0;
size_t        pulseIndex     = PATTERN_LENGTH;  // == PATTERN_LENGTH means idle
unsigned long pulseStartedAt = 0;
bool          motorRunning   = false;

// How long the motor actually runs inside a slot of the given length.
uint16_t onTimeFor(const Pulse& pulse) {
  uint16_t gap = pulse.durationMs * GAP_PERCENT / 100;
  if (gap < MIN_GAP_MS) {
    gap = MIN_GAP_MS;
  }
  return pulse.durationMs > gap ? pulse.durationMs - gap : 0;
}

void driveMotor(bool on) {
  motorRunning = on;
  digitalWrite(motorPin, on ? HIGH : LOW);
}

}  // namespace

void vibrationBegin(uint8_t pin) {
  motorPin = pin;
  pinMode(motorPin, OUTPUT);
  digitalWrite(motorPin, LOW);
}

void vibrationStart() {
  pulseIndex     = 0;
  pulseStartedAt = millis();
  driveMotor(PATTERN[0].vibrates);
}

void vibrationStop() {
  pulseIndex = PATTERN_LENGTH;
  driveMotor(false);
}

bool vibrationIsActive() {
  return pulseIndex < PATTERN_LENGTH;
}

void vibrationUpdate() {
  if (!vibrationIsActive()) {
    return;
  }

  const Pulse& pulse  = PATTERN[pulseIndex];
  unsigned long since = millis() - pulseStartedAt;

  // Inside the slot: drop into the trailing gap once the pulse is up.
  if (motorRunning && since >= onTimeFor(pulse)) {
    driveMotor(false);
  }

  if (since < pulse.durationMs) {
    return;
  }

  // Slot finished - move to the next pulse, or stop after the last one.
  pulseStartedAt = millis();
  if (++pulseIndex >= PATTERN_LENGTH) {
    vibrationStop();
    return;
  }
  driveMotor(PATTERN[pulseIndex].vibrates);
}
