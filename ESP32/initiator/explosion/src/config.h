#pragma once

#include <stdint.h>

// Every tunable number in this firmware. One file, so a literal number in a
// .cpp is a review comment rather than something to go hunting for.
//
// Pins are in board_pins.h, which is the same rule applied to the hardware.

// --- identity -------------------------------------------------------------

// Defaults only. The real values live in NVS and are set per board with the
// serial shell, so one image serves every node.
constexpr uint16_t DefaultNodeId = 1;

// The auto-arm timeout. Five minutes is a guess about how long setting up
// takes, which is a field question rather than a build one - so it is stored in
// NVS and settable with `set autoarm <seconds>`.
constexpr uint32_t DefaultAutoArmSeconds = 300;

// A node that has never been provisioned derives its serial from the chip MAC,
// so there is never a node without one.
constexpr const char *SerialPrefix = "IN-";

// --- radio ----------------------------------------------------------------

constexpr long LoraFrequencyHz = 868E6; // must match the controller and the hardware
constexpr int LoraSpreadingFactor = 7;
constexpr long LoraSignalBandwidthHz = 125E3;
constexpr uint8_t LoraSyncWord = 0x12;
constexpr int LoraTxPowerDbm = 17;

// How many times an unsolicited state announcement is repeated, and how far
// apart. Unacknowledged, because a single unacknowledged frame is a coin toss
// and an ACK path for it would double the protocol to improve a fallback. The
// real backstop is that the next command's ACK carries the true state.
constexpr uint8_t StateAnnounceRepeats = 3;
constexpr uint32_t StateAnnounceIntervalMs = 400;

// --- state ----------------------------------------------------------------

// How long a LED shows a refused command before the display goes back to the
// state. Long enough to notice, short enough not to hide the truth.
constexpr uint32_t RejectFlashMs = 600;

// --- network --------------------------------------------------------------

constexpr uint32_t WifiConnectTimeoutMs = 20000;
constexpr uint32_t WifiRetryIntervalMs = 15000;

// The free Render plan sleeps after about fifteen minutes idle and takes tens
// of seconds to wake, so the first request after a quiet spell is slow rather
// than broken. A short timeout here turns a sleeping service into a node that
// reports itself unreachable.
constexpr uint32_t HttpTimeoutMs = 30000;

constexpr uint32_t HealthReportIntervalMs = 30000;

// How often the node asks whether the dashboard has queued anything. This is
// the latency on a dashboard command, so it is the number to change if that
// feels slow - at the cost of battery and of waking a sleeping service more
// often.
constexpr uint32_t CommandPollIntervalMs = 10000;

// Cap on one upload. The service refuses a larger batch, and a node with a long
// backlog should drain it over several requests rather than one that times out.
constexpr uint8_t MaxEventsPerUpload = 32;
constexpr uint8_t MaxLogsPerUpload = 32;

// --- buffers --------------------------------------------------------------

// Transitions and log records waiting for the network. Offline is not an error
// for this node: it keeps working and keeps a record, and the record is what
// these hold until the connection comes back.
//
// When one fills, the OLDEST entry is dropped. Losing the start of a long
// outage is better than losing the end, because the end is what is happening
// now.
constexpr uint8_t EventBufferSize = 64;
constexpr uint8_t LogBufferSize = 64;

// --- tasks ----------------------------------------------------------------
// Queue depths are small on purpose. These queues carry events, not data: if
// one ever backs up past a handful, something downstream has stopped, and a
// deeper queue would only delay noticing.

constexpr uint8_t StateQueueDepth = 8;
constexpr uint8_t LedQueueDepth = 4;
constexpr uint8_t UiQueueDepth = 8;
constexpr uint8_t RadioTxQueueDepth = 8;

constexpr uint32_t RadioTaskStack = 4096;
constexpr uint32_t StateTaskStack = 4096;
constexpr uint32_t LedTaskStack = 2048;
constexpr uint32_t UiTaskStack = 4096;
constexpr uint32_t NetTaskStack = 8192; // TLS needs the room
constexpr uint32_t ShellTaskStack = 4096;

// Above the idle task, below the radio. The state task outranks the display
// and the network for the same reason it owns the state: nothing should be
// able to delay a SAFE.
constexpr uint8_t RadioTaskPriority = 4;
constexpr uint8_t StateTaskPriority = 3;
constexpr uint8_t LedTaskPriority = 2;
constexpr uint8_t UiTaskPriority = 2;
constexpr uint8_t NetTaskPriority = 1;
constexpr uint8_t ShellTaskPriority = 1;

// --- display --------------------------------------------------------------

constexpr uint8_t OledWidth = 128;
constexpr uint8_t OledHeight = 64;
constexpr uint8_t OledI2cAddress = 0x3C;
constexpr uint32_t UiRefreshMs = 250;

// --- shell ----------------------------------------------------------------

constexpr uint8_t ShellLineMax = 96;
constexpr uint32_t ShellPollMs = 20;
