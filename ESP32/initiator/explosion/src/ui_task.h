#pragma once

#include "events.h"

// The OLED: the state in large text, what last arrived and whether it was
// accepted, the countdown while one is running, and the two links.

void uiTaskStart();

// Safe from any task.
void uiPost(const UiUpdate &update);

// Told by the net task, so the screen can say whether this node can currently
// reach anything.
void uiSetWifiUp(bool up);
void uiSetRadioUp(bool up);
