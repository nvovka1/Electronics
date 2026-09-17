#pragma once

// A serial shell, so a board can be commissioned and inspected over the cable
// without a rebuild. Type `help`.
//
// The commands that drive the state machine are compiled in only when
// BUILD_TEST_COMMANDS is set, which the field build does not set: a way to
// command a node that bypasses both the radio and the backend belongs on a
// bench and nowhere else.

void shellTaskStart();
