#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A fixed-capacity ring of log records. New entries overwrite the oldest, so
// logging never stops and never allocates.
//
// A record is twelve bytes because in the field the log travels over the air
// and lands in a small flash: a formatted line costs about 62 bytes for the
// same fact. The text is reconstructed on the desk from docs/log_dict.csv.
//
// This file is deliberately free of locking and of Arduino: the caller wraps
// ring_push() in whatever critical section its platform needs, and the host
// tests exercise the wrap arithmetic with no hardware.

typedef struct {
  uint32_t ts_ms; // monotonic milliseconds since boot
  uint8_t lvl;
  uint8_t tag;
  uint8_t code;
  uint8_t rsv; // keeps the record 12 bytes with no implicit padding
  uint32_t arg;
} log_rec_t;

typedef struct {
  log_rec_t *slots;
  uint16_t capacity;
  uint16_t count;  // valid records held, saturating at capacity
  uint32_t pushed; // total ever pushed; pushed - count is what was lost
} log_ring_t;

void ring_init(log_ring_t *r, log_rec_t *storage, uint16_t capacity);
void ring_push(log_ring_t *r, const log_rec_t *rec);
void ring_clear(log_ring_t *r);

uint16_t ring_count(const log_ring_t *r);
uint32_t ring_dropped(const log_ring_t *r);

// index 0 is the newest record. Returns false once index reaches count.
// The thirty lines before a fall are the valuable part, so reading newest
// first is the access pattern that matters.
bool ring_peek_newest(const log_ring_t *r, uint16_t index, log_rec_t *out);
