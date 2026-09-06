#pragma once

#include <Arduino.h>

// Boot bookkeeping and the degraded mode it can force.
//
// A node that falls over every minute never stays up long enough to receive
// the update that would cure it. So three abnormal boots in a row change the
// behaviour: the parts that can crash are left switched off, and the parts
// that let someone diagnose and fix it - the shell, the radio, telemetry -
// stay up.
//
// The counter lives in RTC memory, which survives a reset but not a power cut,
// and is mirrored into NVS so a cold start sees it too.

void safeModeBegin();

// Called periodically. Clears the counter once the node has been up for ten
// minutes without falling over. Clearing it at the top of setup() instead
// would be the same as not having a counter at all.
void safeModeTick();

bool safeModeActive();

// Consecutive abnormal boots: panics, watchdogs and brownouts. A clean power
// cycle or a `reboot` command does not count towards it.
uint8_t abnormalBootCount();

// Consecutive brownout resets specifically.
//
// This one is separated out from the abnormal count because it names its own
// cure. A brownout is not a software fault: it is the supply failing to deliver
// what the firmware just asked for, and on this board that is almost always the
// WiFi transmitter's current spike on a thin USB cable with no battery fitted.
// Retrying the same thing harder is how a node spends the rest of its life in a
// reset loop, so the network task reads this and backs off instead.
//
// Cleared by a deliberate power cycle and by ten minutes of clean uptime, the
// same two events that clear the abnormal streak.
uint8_t brownoutStreak();

// Every boot ever, abnormal or not. This is what `version` prints as
// "reboots" and what the health frame carries: a silent reboot is otherwise
// completely invisible.
uint32_t totalBootCount();

esp_reset_reason_t lastResetReason();
const char *lastResetReasonName();

// The reset reason as a small number, for the one byte the health frame has.
uint8_t lastCrashCode();
