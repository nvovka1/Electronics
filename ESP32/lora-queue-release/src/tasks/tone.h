#pragma once

#include <Arduino.h>

bool toneBegin();

// The sidetone runs in its own task, because playing it takes up to 360 ms of
// vTaskDelay. That delay used to sit inside the radio task, where it stopped
// the receiver polling for the whole beep and made back-to-back frames go
// missing. Feedback is never worth a dropped packet.
bool toneTaskStart(UBaseType_t priority, BaseType_t core);

// Non-blocking: queues a beep and returns. A full queue drops the beep, which
// is the right trade - the symbol has already been delivered.
void toneRequest(char symbol);

// Direct control, used by the button task for the live key-down sidetone.
bool toneTryStart();
void toneStop();
