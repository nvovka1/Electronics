#pragma once

#include <Arduino.h>

bool ledTaskStart(QueueHandle_t commandQueue, UBaseType_t priority, BaseType_t core);
