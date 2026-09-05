#pragma once

#include <Arduino.h>

#include "app/app_events.h"

// SPI + LoRa, before any task is started. Applies the frequency, spreading
// factor, coding rate, sync word and TX power from the config, and turns on
// the chip's own payload CRC.
bool radioBegin();

// Three attempts, because an SPI or supply glitch at boot is transient far
// more often than a dead radio is. Returns false only when all three fail.
bool radioBeginWithRetries(uint8_t attempts);

bool radioTaskStart(UBaseType_t priority, BaseType_t core);

// Queues one symbol ('.' or '-') for transmission, carrying the timestamps of
// the press that produced it. False = queue full.
bool radioSendSymbol(char symbol, const EventStamp &stamp, TickType_t timeout);

// Queues a health frame out of turn. The task also sends one every
// health_period_s on its own.
bool radioRequestHealth();

int radioLastRssi();
float radioLastSnr();

uint32_t radioTxCount();
uint32_t radioRxCount();
uint32_t radioNoAckCount();
uint32_t radioBadFrameCount();
uint32_t radioDupCount();

void radioPrintStats(Print &out);

// Hex dump of the last frame sent and the last one received, byte by byte.
// This is what produces the worked example in docs/PROTOCOL.md section 6, and
// it is the fastest way to settle an argument about what is actually on air.
void radioPrintLastFrames(Print &out);
