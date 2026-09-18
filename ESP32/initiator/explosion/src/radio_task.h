#pragma once

#include "events.h"

// The SX1276. Owns the radio: nothing else touches it, which is why there is no
// lock around it.
//
// Receives command frames, checks that they are for this node and that the
// counter is new, and posts them to the state task. Transmits the ACKs and the
// unsolicited state announcements the state task asks for.

void radioTaskStart();

// Queue something to transmit. Safe from any task, never blocks.
void radioPost(const RadioTx &message);

// The controller this node last accepted a command from - who an unsolicited
// announcement should go to. Zero before the first command, in which case the
// announcement is broadcast.
uint16_t radioLastControllerId();

bool radioIsReady();

// The RSSI of the last frame this node received, in dBm. Read through here
// rather than from the LoRa driver directly: the radio task owns that
// peripheral, and a second task calling into the driver while this one is
// mid-transmit is the kind of thing that works on a bench for a month.
int radioLastRssi();

// Put the SX1276 to sleep, and wake it back into receive.
//
// Used while WiFi is being brought up: the LoRa receiver draws about 12 mA, and
// on a supply that is only just failing, every milliamp something else is not
// taking is a milliamp available to the WiFi front end at the moment it powers
// up. The node is deaf to commands for those couple of seconds, which is the
// trade - and it is only made on a board that has already browned out.
void radioStandDown();
void radioStandUp();

// Change transmit power at run time, for finding out what a link actually
// needs without a rebuild between attempts. Not persisted: once you know the
// answer it belongs in config.h. PA_BOOST on this board, so 2..17 dBm.
void radioSetTxPower(int dbm);

int radioTxPower();
