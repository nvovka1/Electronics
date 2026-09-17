#include "event_buffer.h"

#include <Arduino.h>

#include "config.h"

namespace {

StateEventRecord buffer[EventBufferSize];
uint8_t head = 0;  // next slot to write
uint8_t count = 0; // entries waiting
uint16_t dropped = 0;

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;

uint8_t oldestIndex() {
  return (uint8_t)((head + EventBufferSize - count) % EventBufferSize);
}

} // namespace

void eventBufferInit() {
  portENTER_CRITICAL(&lock);
  head = 0;
  count = 0;
  dropped = 0;
  portEXIT_CRITICAL(&lock);
}

void eventBufferPush(const StateEventRecord &record) {
  portENTER_CRITICAL(&lock);

  buffer[head] = record;
  head = (uint8_t)((head + 1) % EventBufferSize);

  if (count < EventBufferSize) {
    count++;
  } else {
    dropped++;
  }

  portEXIT_CRITICAL(&lock);
}

uint8_t eventBufferPeek(StateEventRecord *out, uint8_t max) {
  if (out == nullptr || max == 0) return 0;

  portENTER_CRITICAL(&lock);

  const uint8_t taken = count < max ? count : max;
  uint8_t index = oldestIndex();

  for (uint8_t i = 0; i < taken; i++) {
    out[i] = buffer[index];
    index = (uint8_t)((index + 1) % EventBufferSize);
  }

  portEXIT_CRITICAL(&lock);

  return taken;
}

void eventBufferCommit(uint8_t committed) {
  portENTER_CRITICAL(&lock);

  // Never below zero. A commit larger than what is held can only come from a
  // caller bug, and clamping keeps that from turning into a buffer that
  // believes it holds 65000 entries.
  count = committed >= count ? 0 : (uint8_t)(count - committed);

  portEXIT_CRITICAL(&lock);
}

uint8_t eventBufferPending() {
  portENTER_CRITICAL(&lock);
  const uint8_t pending = count;
  portEXIT_CRITICAL(&lock);
  return pending;
}

uint16_t eventBufferDropped() {
  portENTER_CRITICAL(&lock);
  const uint16_t lost = dropped;
  portEXIT_CRITICAL(&lock);
  return lost;
}
