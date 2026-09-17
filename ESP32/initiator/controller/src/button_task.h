#pragma once

#include "events.h"

// The three buttons. Debounces them and posts one event per press - nothing
// else. No long-press, no double-press, no chords.

void buttonTaskStart();

// Blocks until a button is pressed, or the timeout expires. Returns false on
// timeout. Called only by the command task, which is the one thing that acts on
// a press.
bool buttonWait(ButtonEvent *out, uint32_t timeoutMs);

// Whether SAFE has been pressed and not yet acted on.
//
// Exists so the command task can abandon a retry loop the moment SAFE is
// pressed. Without it, pressing SAFE while a FIRE is retrying means waiting for
// three attempts to time out first - about two seconds of a safety control
// doing nothing because the radio is busy failing at something else.
bool buttonSafePending();
