#pragma once

#include "events.h"

// The OLED: which node is being commanded, what we believe it is doing, what
// the last exchange came to, and the link.

void uiTaskStart();

// Safe from any task.
void uiPost(const UiUpdate &update);
