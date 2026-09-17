#pragma once

#include "events.h"

// The sole owner of this node's state.
//
// Both command sources - the radio and the backend poll - post onto the one
// queue this task reads, so they serialise: if two commands arrive together the
// second is evaluated against the state the first produced, not against the
// state both of them saw. That is the whole reason the state is not a variable
// other tasks can touch.

void stateTaskStart();

// Offer a command to the node. Safe from any task. Returns false only if the
// queue is full, which would mean the state task has stopped.
bool stateSubmitCommand(const CommandRequest &request);

// The state right now, for the shell and for a health report. A snapshot: it
// can be stale the instant it returns, which is why nothing decides anything
// with it.
uint8_t stateCurrent();

// Milliseconds left on the auto-arm countdown, or 0 when none is running.
uint32_t stateCountdownRemainingMs();
