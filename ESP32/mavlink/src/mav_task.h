#pragma once

#include <stdint.h>

// The MAVLink link: owns UART2, decodes the stream into the shared snapshot,
// announces us to the flight controller and asks for the messages we want.
//
// Nothing here writes to the flight log. A parser that also decided what to
// record would have to know about disk space and upload cursors, and the one
// thing this task must always do is keep draining the UART - the receive buffer
// is 2 kB and a MAVLink stream fills it in well under a second.

void mavTaskStart();

// Milliseconds since the last heartbeat from the autopilot, or UINT32_MAX when
// there has never been one. Shown on the board's page and in the shell, because
// "is it plugged in the right way round" is the question this firmware is asked
// most often.
uint32_t mavLinkAgeMs();

uint32_t mavMessagesSeen();

// Raw bytes off the UART, whether or not they decoded into anything. The one
// number that separates the two failures that look identical from every other
// angle: zero bytes means nothing is arriving at all - wrong pads, no GND, a
// disabled port - while bytes climbing with no messages means the bytes are
// arriving and are gibberish, which is the wrong baud rate.
uint32_t mavBytesSeen();

// Frames the parser rejected: bad CRC or a length that did not match. A handful
// at power-on is normal - the UART starts mid-frame. A number that climbs with
// the messages means the baud rate is wrong.
uint32_t mavParseErrors();
