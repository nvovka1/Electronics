#pragma once

#include <Arduino.h>

bool radioBegin();   // SPI + LoRa, before any task is started
bool radioTaskStart(UBaseType_t priority, BaseType_t core);

// Queues one symbol ('.' or '-') for transmission. False = queue full.
bool radioSendSymbol(char symbol, TickType_t timeout);
