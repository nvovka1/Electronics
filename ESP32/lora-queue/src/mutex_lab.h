#pragma once

#include <Arduino.h>

// Part 2 of the exercise: multitasking broken on purpose.
//
// The lab is armed by a TRIPLE click on the button and never disarms - that is
// the point. Which scenario is armed is chosen at build time with -D SCENARIO
// (see platformio.ini):
//
//   1 = AB/BA deadlock          - two tasks, two mutexes, opposite lock order
//   2 = priority inversion      - a spinning high-priority task starves core 0
//   3 = critical section        - the mutex holder disables interrupts
//
// The blink tasks from part 1 keep running, so the LED itself tells you how far
// the damage spread: still blinking = only the lab tasks died; frozen = that
// core died; restarted from 250 ms = the chip rebooted.
const char* mutexLabName();

// Creates the broken tasks. Ignores every call after the first.
void mutexLabStart();
