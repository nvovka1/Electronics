#pragma once

#include "events.h"

// The SX1276. Owns the radio: nothing else touches it, which is why there is no
// lock around it.
//
// Transmission is synchronous - radioSendCommand blocks the caller until the
// packet is out - because the only caller is the command task, and it has
// nothing useful to do until the command has actually been sent.

void radioTaskStart();

bool radioIsReady();

// Sends one command frame. Returns false only if the radio is not up.
bool radioSendCommand(uint16_t destination, uint8_t command, uint32_t counter);

// Waits for an ACK or an announcement. Returns false on timeout.
bool radioWait(RadioRx *out, uint32_t timeoutMs);

// Throws away anything already waiting. Called before sending, so an ACK for a
// command we have given up on cannot be mistaken for the answer to the next
// one - the counter check would catch it, but not until after the screen had
// flickered.
void radioDrain();

int radioLastRssi();

// Change transmit power at run time, for finding out what a link actually
// needs without a rebuild between every attempt. Not persisted: once you know
// the answer it belongs in config.h, where the next person will see it.
//
// PA_BOOST on this board, so the usable range is 2..17 dBm.
void radioSetTxPower(int dbm);

int radioTxPower();
