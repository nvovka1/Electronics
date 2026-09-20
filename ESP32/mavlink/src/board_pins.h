#pragma once

// Plain ESP32 DevKit (WROOM-32). Three wires to the flight controller.
//
//   GPIO16  UART2 RX  <-  TX pad of a free UART on the F405
//   GPIO17  UART2 TX  ->  RX pad of the same UART
//   GND               <-> GND
//
// The ESP32 is powered separately - see WIRING.md. GND is still required: UART
// is single-ended, and without a shared reference the idle level at these pins
// is undefined, which looks exactly like a wrong baud rate.
//
// GPIO16/17 ARE ONLY FREE ON WROOM MODULES. On a WROVER they are wired to the
// PSRAM and using them here produces a board that boots and then behaves
// strangely rather than one that fails honestly. The alternates below are safe
// on both; switch by changing these two lines.

static const int MavRxPin = 16;
static const int MavTxPin = 17;

// static const int MavRxPin = 25;
// static const int MavTxPin = 27;
