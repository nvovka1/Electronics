#pragma once

#include <Arduino.h>

// The UART shell. Not a test mode - a test mode is the thing that gets shut
// off in the field image. This is the set of commands without which a deployed
// node is unusable: it can name itself, re-run its self-test, hand over its
// log and show and change its settings.
//
// It runs in its own task so it answers even while the radio is busy waiting
// out an ACK timeout. A node whose shell goes quiet when something is wrong is
// silent exactly when it is needed.

bool shellTaskStart(UBaseType_t priority, BaseType_t core);

// Exposed so setup() can print the same banner the `help` command does.
void shellPrintBanner(Print &out);
