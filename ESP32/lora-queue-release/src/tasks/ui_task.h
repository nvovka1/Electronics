#pragma once

#include <Arduino.h>

#include "app/app_events.h"

bool uiBegin();   // I2C + OLED, before any task is started
bool uiTaskStart(UBaseType_t priority, BaseType_t core);

void uiPostBanner(const char* text);      // replace the status line

// Both carry the origin timestamps so the UI task can report - and show - how
// long the symbol took to reach the screen.
void uiPostSymbolSent(char symbol, const EventStamp& stamp);
void uiPostSymbolReceived(char symbol, uint32_t radioAtMs);

// The identity page: serial, node id, firmware version and build, POST mask
// and battery. Reachable in the field by holding the key, and over UART with
// `screen info`, so nobody has to guess which node they are holding or which
// build is on it.
void uiShowInfoPage();
void uiShowMainPage();
void uiToggleInfoPage();
void uiRefresh();

// Drawn before any task starts, straight from setup(), so the very first thing
// the screen ever shows is what this node is and whether it passed its POST.
void uiDrawSplash();

// Does the panel still acknowledge on I2C? Under the same lock the UI task
// draws with: `self-test` runs from the shell task, and two masters on one
// bus is how an I2C transaction gets corrupted.
bool uiProbePanel();
