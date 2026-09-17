#pragma once

#include "events.h"

// Transitions waiting to reach the backend.
//
// The node applies a transition, lights the LED and draws the screen whether or
// not WiFi is up; this is where the record of it waits until the connection
// comes back. A node out of coverage is a node that still works, with a gap in
// its record rather than a fault.
//
// When it fills, the OLDEST entry is dropped. Losing the start of a long outage
// is better than losing the end, because the end is what is happening now.

void eventBufferInit();

// Safe from any task.
void eventBufferPush(const StateEventRecord &record);

// Oldest first. Returns how many were written into `out`. They are NOT removed
// until eventBufferCommit, so an upload that fails leaves them in place.
uint8_t eventBufferPeek(StateEventRecord *out, uint8_t max);

// Drops the oldest `count` entries, after the backend has confirmed it stored
// them. Peek-then-commit rather than take-then-retry, because a node that took
// records and then failed to send would have to hold them somewhere else -
// which is this buffer, one function call later.
void eventBufferCommit(uint8_t count);

uint8_t eventBufferPending();

// Entries lost to the buffer wrapping before they could be sent.
uint16_t eventBufferDropped();
