#pragma once

#include <stdint.h>

#include "sequence.h"

// Decides what to send, sends it, and believes only what comes back.
//
// The belief about the node's state is updated from an ACK or an announcement
// and from nothing else. A controller that advances its own display on an
// unacknowledged send is a controller that lies - and the operator's next press
// would then be aimed at a state the node is not in.

void commandTaskStart();

// Snapshots, for the shell and the screen.
uint16_t commandTarget();
node_state_t commandBelievedState();
belief_t commandBelief();
