#pragma once

#include <Arduino.h>

bool buttonTaskStart(QueueHandle_t commandQueue, UBaseType_t priority, BaseType_t core);
