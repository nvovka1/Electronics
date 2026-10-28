#pragma once

// Starts the on-board SX1276 with the LORA_* settings from config.h.
// False if the radio doesn't answer.
bool startLoraRadio();
