#pragma once

#include <Arduino.h>

// What this controller knows about itself that survives a power cut.
//
// Deliberately NOT here: the believed state of any node. A controller boots
// knowing nothing and asks - restoring a belief from flash would put a stale
// state on the screen and let the first button press send the wrong command.

struct Settings {
  uint16_t nodeId;      // this controller's own address, the SRC in every frame
  uint16_t targetId;    // the node currently being commanded
  uint16_t maxTargetId; // the TARGET button cycles 1..this
  uint32_t bootCount;
};

extern Settings settings;

void settingsLoad();

bool settingsSaveNodeId(uint16_t nodeId);
bool settingsSaveTargetId(uint16_t targetId);
bool settingsSaveMaxTargetId(uint16_t maxTargetId);
bool settingsReset();

// The next command counter, monotonic and never reused.
//
// Reserved in blocks so NVS is not written on every button press: a block is
// claimed at boot and handed out from RAM. A power cut costs the rest of the
// block, which is the right way round - skipping counters is harmless, and
// repeating one makes the node refuse the command as a replay.
uint32_t counterNext();

// What the counter has reached, for the shell.
uint32_t counterCurrent();
