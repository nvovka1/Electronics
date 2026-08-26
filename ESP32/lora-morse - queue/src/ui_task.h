#pragma once

#include <Arduino.h>

#include "app_events.h"

bool uiBegin();   // I2C + OLED, before any task is started
bool uiTaskStart(UBaseType_t priority, BaseType_t core);

void uiPostBanner(const char* text);      // replace the status line
void uiPostSymbolSent(char symbol);       // append to the TX line
void uiPostSymbolReceived(char symbol);   // append to the RX line

void uiShowFatal(const char* message);
