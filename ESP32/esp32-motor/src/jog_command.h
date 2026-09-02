#pragma once

#include "stepper.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// What the joystick task publishes: not an event, but the current state of the
// stick. "Turn this way, this fast" - or "stop".
struct JogCommand {
    bool moving;
    MoveDirection direction;
    uint32_t stepIntervalUs;
};

// A one-slot mailbox, not an event queue. The joystick task uses
// xQueueOverwrite, so a new reading replaces the previous one instead of
// queueing behind it, and the motor task always acts on the freshest value.
//
// This is the difference from the old button design: a button press is an event
// that must not be lost, so it belonged in a queue. Stick position is state,
// where only the latest value has any meaning and stale ones are worse than
// useless.
extern QueueHandle_t jogMailbox;
