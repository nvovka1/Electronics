#pragma once

#include <Arduino.h>

#include "net/fleet_client.h"

// Over-the-air update, and the rules that stop one from turning a node into a
// paperweight.
//
// The board has two application slots, app0 and app1, 1.25 MB each. The
// running image is never the one being written: a new image goes into the
// other slot and only becomes the boot target once every byte of it has been
// verified. Pull the power at any point during the download and the node comes
// back on the image it was already running, because nothing about which slot
// boots has changed yet.
//
// What happens after the switch is the part that matters, and it is a
// three-layer net:
//
//   1. The bootloader marks a freshly switched image PENDING_VERIFY. If it
//      panics, hangs the watchdog or browns out before confirming itself, the
//      next boot goes back to the previous image automatically. This is the
//      layer that survives an image that does not work at all.
//
//   2. This firmware overrides verifyRollbackLater() so Arduino does NOT
//      confirm the image the instant setup() is reached. Confirmation is
//      earned: a clean critical POST, and a successful check-in with the fleet
//      service. An image that boots but cannot talk to anything is exactly the
//      one you can never fix remotely, so it is treated as a failure.
//
//   3. If the trial window passes without that, the node rolls itself back and
//      records the version that failed, so the service offering it again does
//      not start an update loop that flattens the battery.
//
// The preconditions before a download are just as important, and the one that
// actually bricks devices is the battery: writing a third of a megabyte of
// flash and then rebooting into an image that has never run is the hungriest
// and least interruptible thing this node ever does.

typedef enum {
  OTA_GATE_OK = 0,
  OTA_GATE_DISABLED,          // config: ota_enabled is 0
  OTA_GATE_ON_TRIAL,          // we are on probation ourselves; finish that first
  OTA_GATE_LOW_BATTERY,
  OTA_GATE_BATTERY_UNTRUSTED, // the ADC check failed; no reading, no update
  OTA_GATE_WRONG_HARDWARE,    // an image for a different board revision
  OTA_GATE_SAME_VERSION,
  OTA_GATE_TOO_BIG,           // will not fit the inactive slot
  OTA_GATE_LOW_HEAP,
  OTA_GATE_BLOCKED_VERSION,   // this exact version already failed its trial
  OTA_GATE_NO_MANIFEST,
} ota_gate_t;

typedef enum {
  OTA_ROLLBACK_TRIAL_TIMEOUT = 1, // never managed to check in
  OTA_ROLLBACK_POST_FAILED = 2,   // a critical block failed on the new image
  OTA_ROLLBACK_OPERATOR = 3,      // somebody typed `ota rollback`
} ota_rollback_reason_t;

typedef enum {
  OTA_IDLE = 0,
  OTA_DOWNLOADING,
  OTA_VERIFYING,
  OTA_STAGED, // written and verified; the reboot is the next instruction
  OTA_FAILED,
  OTA_ON_TRIAL, // this boot is the first of a new image
} ota_state_t;

// Classifies this boot. Called before any task starts and before the network
// exists, because whether the running image is on probation decides what the
// screen says and what the node is allowed to do.
void otaBegin();

bool otaIsOnTrial();
uint32_t otaTrialSecondsLeft();

// Driven by the network task once per cycle. checkedIn is true when this node
// has just had a successful exchange with the fleet service - the evidence
// that the new image can still be reached and therefore still be fixed.
void otaTick(bool checkedIn);

ota_gate_t otaCheckGates(const ota_manifest_t &manifest);
const char *otaGateText(ota_gate_t gate);

// Downloads, hashes, writes and switches the boot slot, then reboots. Returns
// only on failure: on success the node is already restarting.
bool otaApply(const ota_manifest_t &manifest);

ota_state_t otaState();
uint8_t otaProgressPercent();

// Marks the running image good even though the trial has not finished on its
// own. For an operator standing in front of the node with a cable, who can
// see it is fine and does not want it rolled back in eight minutes.
bool otaConfirmNow();

// Goes back to the previous image deliberately. False when there is no
// previous image to go back to, which is the case on a node that has never
// been updated over the air.
bool otaRollbackNow(ota_rollback_reason_t reason);

void otaPrint(Print &out);
