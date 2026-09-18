#pragma once

#include <stdint.h>

// Every tunable number in this firmware. One file, so a literal number in a
// .cpp is a review comment rather than something to go hunting for.
//
// Pins are in board_pins.h, which is the same rule applied to the hardware.

// --- identity -------------------------------------------------------------

// Defaults only. The real values live in NVS, set per board with the shell, so
// one image serves every controller.
constexpr uint16_t DefaultNodeId = 100; // controllers are numbered from 100
constexpr uint16_t DefaultTargetId = 1;

// BTN_TARGET cycles through 1..this. Raise it with `set targets <n>` when there
// are more nodes than that in the field.
constexpr uint16_t DefaultMaxTargetId = 4;

// --- radio ----------------------------------------------------------------
// Must match the node exactly. A controller on a different spreading factor is
// a controller nothing hears, and nothing about the failure says so.

constexpr long LoraFrequencyHz = 868E6;
constexpr int LoraSpreadingFactor = 7;
constexpr long LoraSignalBandwidthHz = 125E3;
constexpr uint8_t LoraSyncWord = 0x12;
constexpr int LoraTxPowerDbm = 17;

// How long to wait for the ACK before sending again, and how many times to try
// in total. Three attempts at 700 ms is about two seconds before the controller
// admits it has lost the node - long enough to ride out one collision, short
// enough that nobody stands there wondering.
constexpr uint32_t AckTimeoutMs = 700;
constexpr uint8_t SendAttempts = 3;

// --- counter --------------------------------------------------------------

// Every command carries a counter the node remembers, so a recorded frame
// cannot be replayed. It must never go backwards - a counter that restarts
// makes the node refuse everything as a replay until it catches up.
//
// Writing NVS on every press would wear the flash, so the counter is reserved
// in blocks: a block is claimed at boot and the numbers in it are handed out
// from RAM. A power cut costs the rest of the block, which is the right trade -
// skipping numbers is harmless, repeating one is not.
constexpr uint32_t CounterBlockSize = 100;

// --- buttons --------------------------------------------------------------

// How long a level must HOLD before it is believed. Long enough to swallow
// contact bounce, short enough that a press feels immediate. These are
// mechanical buttons, so this is about the switch rather than the person.
constexpr uint32_t ButtonDebounceMs = 40;
constexpr uint32_t ButtonPollMs = 10;

// The shortest gap between two accepted presses of the same button. Nobody
// presses one four times a second, so anything faster is hardware misbehaving -
// a floating input, most likely - and this keeps that from filling the queue
// and starving the buttons that are wired correctly.
constexpr uint32_t ButtonMinGapMs = 250;

// --- display --------------------------------------------------------------

constexpr uint8_t OledWidth = 128;
constexpr uint8_t OledHeight = 64;
constexpr uint8_t OledI2cAddress = 0x3C;
constexpr uint32_t UiRefreshMs = 200;

// How long a "sent / refused / lost" line stays on the screen before it goes
// back to just showing the state.
constexpr uint32_t ResultLingerMs = 4000;

// --- tasks ----------------------------------------------------------------

constexpr uint8_t ButtonQueueDepth = 8;
constexpr uint8_t UiQueueDepth = 8;
constexpr uint8_t RadioRxQueueDepth = 8;

constexpr uint32_t ButtonTaskStack = 2048;
constexpr uint32_t CommandTaskStack = 4096;
constexpr uint32_t RadioTaskStack = 4096;
constexpr uint32_t UiTaskStack = 4096;
constexpr uint32_t ShellTaskStack = 4096;

// The radio outranks the rest: an ACK that arrives while a lower-priority task
// is drawing the screen must not be missed.
constexpr uint8_t RadioTaskPriority = 4;
constexpr uint8_t CommandTaskPriority = 3;
constexpr uint8_t ButtonTaskPriority = 3;
constexpr uint8_t UiTaskPriority = 2;
constexpr uint8_t ShellTaskPriority = 1;

// --- shell ----------------------------------------------------------------

constexpr uint8_t ShellLineMax = 96;
constexpr uint32_t ShellPollMs = 20;
