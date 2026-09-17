#include "replay_guard.h"

#include <string.h>

void replay_reset(replay_guard_t *guard) {
  if (guard == NULL) return;
  memset(guard, 0, sizeof(*guard));
}

static replay_slot_t *find(replay_guard_t *guard, uint16_t src) {
  for (size_t i = 0; i < REPLAY_SLOTS; i++) {
    if (guard->slots[i].used && guard->slots[i].src == src) return &guard->slots[i];
  }
  return NULL;
}

static replay_slot_t *claim(replay_guard_t *guard, uint16_t src) {
  for (size_t i = 0; i < REPLAY_SLOTS; i++) {
    if (!guard->slots[i].used) {
      guard->slots[i].used = true;
      guard->slots[i].src = src;
      guard->slots[i].last_counter = 0;
      return &guard->slots[i];
    }
  }

  // Every slot taken. Evict the one with the lowest counter: it is the source
  // that has said least, and so the one least likely to be the controller
  // currently in someone's hand.
  replay_slot_t *oldest = &guard->slots[0];
  for (size_t i = 1; i < REPLAY_SLOTS; i++) {
    if (guard->slots[i].last_counter < oldest->last_counter) oldest = &guard->slots[i];
  }

  oldest->src = src;
  oldest->last_counter = 0;
  return oldest;
}

bool replay_accept(replay_guard_t *guard, uint16_t src, uint32_t counter) {
  if (guard == NULL) return false;

  // Zero is what an uninitialised counter looks like, and what a controller
  // that has lost its NVS would send. Refusing it costs one button press and
  // removes a value that would otherwise always compare as "already seen".
  if (counter == 0) return false;

  replay_slot_t *slot = find(guard, src);

  if (slot == NULL) {
    slot = claim(guard, src);
    slot->last_counter = counter;
    return true;
  }

  // Strictly greater. Equal is the replay this exists to catch - and also a
  // controller retransmitting because our ACK was lost, which is why the
  // caller re-sends the ACK for a refused replay rather than staying silent.
  if (counter <= slot->last_counter) return false;

  slot->last_counter = counter;
  return true;
}

uint32_t replay_last_counter(const replay_guard_t *guard, uint16_t src) {
  if (guard == NULL) return 0;

  for (size_t i = 0; i < REPLAY_SLOTS; i++) {
    if (guard->slots[i].used && guard->slots[i].src == src) return guard->slots[i].last_counter;
  }
  return 0;
}
