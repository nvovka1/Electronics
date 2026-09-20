#pragma once

// The serial shell. How a board is commissioned: its name, its network, the
// service it reports to, the baud rate the flight controller is using, and the
// rate it records at.
//
// None of that is a build flag, so one image serves every board and a board is
// re-pointed without a toolchain.

void shellTaskStart();
