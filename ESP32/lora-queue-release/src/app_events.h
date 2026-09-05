#pragma once

#include <Arduino.h>

// Timestamps that travel with an event, so every stage can report how long the
// hop before it took. For a symbol that arrived over the air there is no local
// key press: pressedAtMs stays 0 and keyedAtMs is when the radio read the byte.
struct EventStamp {
  uint32_t pressedAtMs;   // first physical press edge of the gesture
  uint32_t keyedAtMs;     // when the button task classified the gesture
};

// --- Button Task -> main loop ---
enum class KeyPress : uint8_t { Single, Double };

struct KeyEvent {
  KeyPress   press;
  EventStamp stamp;
};

// --- main loop -> Radio Task ---
struct RadioRequest {
  char       symbol;      // '.' or '-'
  EventStamp stamp;
};

// --- any task -> UI Task ---
enum class UiEventKind : uint8_t { Banner, SymbolSent, SymbolReceived };

struct UiEvent {
  UiEventKind kind;
  char        symbol;     // '.' or '-' for SymbolSent / SymbolReceived
  char        text[20];   // Banner only
  EventStamp  stamp;      // zeroed for Banner
};
