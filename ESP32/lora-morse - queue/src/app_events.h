#pragma once

#include <Arduino.h>

// --- Button Task -> main loop ---
enum class KeyPress : uint8_t { Single, Double };

struct KeyEvent {
  KeyPress press;
  uint32_t atMs;      // millis() of the gesture, handy for logging/debugging
};

// --- main loop -> Radio Task ---
struct RadioRequest {
  char symbol;        // '.' or '-'
};

// --- any task -> UI Task ---
enum class UiEventKind : uint8_t { Banner, SymbolSent, SymbolReceived };

struct UiEvent {
  UiEventKind kind;
  char        symbol;     // '.' or '-' for SymbolSent / SymbolReceived
  char        text[20];   // Banner only
};
