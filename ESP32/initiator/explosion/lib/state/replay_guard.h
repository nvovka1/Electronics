#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Remembers the highest command counter seen from each controller, so a frame
// that has been recorded off the air and sent again is refused.
//
// The CRC proves a frame is intact, not that it is new. Without this, anyone
// who captures one FIRE frame can make the node fire again by transmitting the
// same bytes, and every check in the state machine would pass - because the
// frame really did come from the controller, once.
//
// Pure: no Arduino, no storage. Persisting the counters is the caller's job.

#ifdef __cplusplus
extern "C" {
#endif

// One slot per controller this node has heard from. Two is already generous for
// a fleet with one handheld; the table is small and fixed because there is no
// allocation after setup.
#define REPLAY_SLOTS 4

typedef struct {
  uint16_t src;
  uint32_t last_counter;
  bool used;
} replay_slot_t;

typedef struct {
  replay_slot_t slots[REPLAY_SLOTS];
} replay_guard_t;

void replay_reset(replay_guard_t *guard);

// True when this counter is new and the guard has recorded it. False when it
// has been seen before, which is the frame that must not be acted on.
//
// A counter from a source with no slot free evicts the least recently used
// one. That is the safe direction to fail: the worst case is that an old
// controller's replay becomes possible again, rather than a live controller
// being locked out of commanding the node.
bool replay_accept(replay_guard_t *guard, uint16_t src, uint32_t counter);

// What the guard last saw from a source, or 0 when it has never heard from it.
uint32_t replay_last_counter(const replay_guard_t *guard, uint16_t src);

#ifdef __cplusplus
}
#endif
