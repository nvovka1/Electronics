#pragma once

#include <Arduino.h>

// Power-on self-test. Four hundred milliseconds that save a drive into the
// field.
//
// Two rules shape everything here. It never stops and never loops: a failed
// block is a bit and a log line, and the node comes up however it can. And the
// result is a bitmask, not a sentence in the console - the mask goes into the
// log, onto the screen and into the health frame, so an operator can see
// "node 7: radio ok, microphone not" without leaving the desk.

enum PostBit : uint8_t {
  POST_BIT_POWER = 0,
  POST_BIT_NVS = 1,
  POST_BIT_ADC = 2,
  POST_BIT_RADIO = 3,
  POST_BIT_DISPLAY = 4,
  POST_BIT_BUTTON = 5,
  POST_BIT_COUNT = 6,
};

typedef struct {
  const char *name;
  uint8_t bit;
  bool (*check)(void); // true when the block is alive
  bool critical;       // false means the node is still useful without it
  const char *reaction; // what the firmware does when this one fails
} post_item_t;

extern const post_item_t POST_ITEMS[];
extern const size_t POST_ITEM_COUNT;

// Some blocks cannot be probed from a pure function - whether the config came
// out of a valid NVS record, and whether the display answered on I2C, are
// facts established during start-up. They are handed to the POST rather than
// re-derived, so every check stays free of side effects.
void postObserveNvs(bool ok);
void postObserveDisplay(bool ok);
void postObserveRadio(bool ok);

// Runs every check and returns the mask. Zero means everything passed.
uint16_t postRun();

uint16_t postMask();

// The failing bits that belong to critical blocks. Non-zero means the node
// cannot do its primary job, only report on itself.
uint16_t postCriticalMask();

const char *postBlockName(uint8_t bit);
const char *postBlockReaction(uint8_t bit);

// Human-readable breakdown for `self-test` and for the boot log.
void postPrint(Print &out);
