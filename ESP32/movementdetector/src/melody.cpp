#include "melody.h"

namespace {

// --- Note frequencies (Hz) used by the Imperial March intro ---
constexpr uint16_t NOTE_REST = 0;
constexpr uint16_t NOTE_F4   = 349;
constexpr uint16_t NOTE_A4   = 440;
constexpr uint16_t NOTE_C5   = 523;
constexpr uint16_t NOTE_E5   = 659;
constexpr uint16_t NOTE_F5   = 698;

struct Note {
  uint16_t frequencyHz;  // NOTE_REST (0) means silence for the duration
  uint16_t durationMs;   // full slot length, including the gap after the note
};

// "The Imperial March" (Darth Vader's theme), opening phrase.
constexpr Note MELODY[] = {
  {NOTE_A4, 500}, {NOTE_A4, 500}, {NOTE_A4, 500},
  {NOTE_F4, 350}, {NOTE_C5, 150},
  {NOTE_A4, 500}, {NOTE_F4, 350}, {NOTE_C5, 150}, {NOTE_A4, 650},
  {NOTE_REST, 500},
  {NOTE_E5, 500}, {NOTE_E5, 500}, {NOTE_E5, 500},
  {NOTE_F5, 350}, {NOTE_C5, 150},
  {NOTE_A4, 500}, {NOTE_F4, 350}, {NOTE_C5, 150}, {NOTE_A4, 650},
};

constexpr size_t MELODY_LENGTH = sizeof(MELODY) / sizeof(MELODY[0]);

// Fraction of each slot the note actually sounds. The remainder is a short
// silence so that repeated notes of the same pitch stay distinguishable.
constexpr uint8_t SOUND_PERCENT = 90;

// LEDC (hardware PWM) setup. Channel 0 is free - nothing else here uses LEDC.
constexpr uint8_t  LEDC_CHANNEL    = 0;
constexpr uint8_t  LEDC_RESOLUTION = 8;    // bits
constexpr uint32_t LEDC_IDLE_FREQ  = 1000; // placeholder until the first note
constexpr uint32_t HALF_DUTY       = (1u << LEDC_RESOLUTION) / 2;

size_t        noteIndex     = MELODY_LENGTH;  // == MELODY_LENGTH means idle
unsigned long noteStartedAt = 0;
bool          noteSounding  = false;

void soundFrequency(uint16_t frequencyHz) {
  if (frequencyHz == NOTE_REST) {
    ledcWrite(LEDC_CHANNEL, 0);  // duty 0 = silence
    return;
  }
  ledcWriteTone(LEDC_CHANNEL, frequencyHz);
  ledcWrite(LEDC_CHANNEL, HALF_DUTY);  // 50% duty = square wave
}

}  // namespace

void melodyBegin(uint8_t buzzerPin) {
  ledcSetup(LEDC_CHANNEL, LEDC_IDLE_FREQ, LEDC_RESOLUTION);
  ledcAttachPin(buzzerPin, LEDC_CHANNEL);
  ledcWrite(LEDC_CHANNEL, 0);
}

void melodyStart() {
  noteIndex     = 0;
  noteStartedAt = millis();
  noteSounding  = true;
  soundFrequency(MELODY[0].frequencyHz);
}

void melodyStop() {
  noteIndex    = MELODY_LENGTH;
  noteSounding = false;
  ledcWrite(LEDC_CHANNEL, 0);
}

bool melodyIsPlaying() {
  return noteIndex < MELODY_LENGTH;
}

void melodyUpdate() {
  if (!melodyIsPlaying()) {
    return;
  }

  const Note& note    = MELODY[noteIndex];
  unsigned long since = millis() - noteStartedAt;

  // Inside the slot: drop into the trailing gap once the sounding part is up.
  if (noteSounding && since >= (unsigned long)note.durationMs * SOUND_PERCENT / 100) {
    noteSounding = false;
    ledcWrite(LEDC_CHANNEL, 0);
  }

  if (since < note.durationMs) {
    return;
  }

  // Slot finished - move to the next note, or stop after the last one.
  noteStartedAt = millis();
  if (++noteIndex >= MELODY_LENGTH) {
    melodyStop();
    return;
  }
  noteSounding = true;
  soundFrequency(MELODY[noteIndex].frequencyHz);
}
