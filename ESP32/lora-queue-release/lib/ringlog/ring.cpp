#include "ring.h"

#include <string.h>

void ring_init(log_ring_t *r, log_rec_t *storage, uint16_t capacity) {
  if (!r) return;
  r->slots = storage;
  r->capacity = storage ? capacity : 0;
  r->count = 0;
  r->pushed = 0;
}

void ring_clear(log_ring_t *r) {
  if (!r) return;
  r->count = 0;
  r->pushed = 0;
}

void ring_push(log_ring_t *r, const log_rec_t *rec) {
  if (!r || !r->slots || !r->capacity || !rec) return;

  r->slots[r->pushed % r->capacity] = *rec;
  r->pushed++;
  if (r->count < r->capacity) r->count++;
}

uint16_t ring_count(const log_ring_t *r) { return r ? r->count : 0; }

uint32_t ring_dropped(const log_ring_t *r) {
  return r ? (r->pushed - r->count) : 0;
}

bool ring_peek_newest(const log_ring_t *r, uint16_t index, log_rec_t *out) {
  if (!r || !r->slots || !out) return false;
  if (index >= r->count) return false;

  // pushed is the index the NEXT record will take, so the newest sits one
  // before it. Everything is done in the unwrapped counter and folded at the
  // end, which keeps the arithmetic correct across the wrap.
  const uint32_t absolute = r->pushed - 1u - (uint32_t)index;
  *out = r->slots[absolute % r->capacity];
  return true;
}
