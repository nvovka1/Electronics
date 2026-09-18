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

// Blank the panel and bring it back. The OLED draws about 15 mA, which is worth
// having back during the WiFi start-up surge on a marginal supply.
// The last HTTP status from the fleet service, for the screen. This is the
// whole diagnosis on a board that cannot hold a serial cable up.
void uiSetApiStatus(int status);

void uiSuspendPanel();
void uiResumePanel();
