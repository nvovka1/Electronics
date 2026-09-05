#pragma once

#include <Arduino.h>

#include "app_events.h"

bool uiBegin();   // I2C + OLED, before any task is started
bool uiTaskStart(UBaseType_t priority, BaseType_t core);

void uiPostBanner(const char* text);      // replace the status line

// Both carry the origin timestamps so the UI task can report - and show - how
// long the symbol took to reach the screen.
void uiPostSymbolSent(char symbol, const EventStamp& stamp);
void uiPostSymbolReceived(char symbol, uint32_t radioAtMs);

void uiShowFatal(const char* message);
