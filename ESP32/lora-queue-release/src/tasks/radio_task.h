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

// Reads the SX1276 version register over SPI, under the same lock the radio
// task uses. The POST calls this rather than driving the bus itself: the radio
// module owns SPI, and `self-test` runs from the shell task while the radio
// task may be mid-transaction.
bool radioProbeChip();

// Drops the transceiver into its sleep state, and puts it back into continuous
// receive. Used for one thing only: standing the radio down for the few seconds
// the WiFi front end is powering up, on a board whose supply has already given
// way once. The SX1276 draws about 12 mA in receive, which is not much until
// you are 30 mA short of a brownout.
//
// Both take the same SPI lock the radio task uses, so a packet in flight is
// never interrupted mid-transaction. Sleeping the radio means missed frames -
// that is the trade, and it is only made on a node that is otherwise rebooting
// in a loop.
bool radioStandDown();
bool radioStandUp();

// The health frame carries this; every other counter is reported by
// radioPrintStats() and has no caller outside the radio module.
int radioLastRssi();

void radioPrintStats(Print &out);

// Hex dump of the last frame sent and the last one received, byte by byte.
// This is what produces the worked example in docs/PROTOCOL.md section 6, and
// it is the fastest way to settle an argument about what is actually on air.
void radioPrintLastFrames(Print &out);
