#pragma once

#include <Arduino.h>

#include "app_events.h"

bool radioBegin();   // SPI + LoRa, before any task is started
bool radioTaskStart(UBaseType_t priority, BaseType_t core);

// Queues one symbol ('.' or '-') for transmission, carrying the timestamps of
// the press that produced it. False = queue full.
bool radioSendSymbol(char symbol, const EventStamp& stamp, TickType_t timeout);
