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
// Long is not a Morse symbol: it is how the screen is switched to the identity
// page in the field, where there is no laptop to type `screen info` into.
enum class KeyPress : uint8_t { Single, Double, Long };

struct KeyEvent {
  KeyPress   press;
  EventStamp stamp;
};

// --- main loop -> Radio Task ---
enum class RadioRequestKind : uint8_t { Symbol, Health };

struct RadioRequest {
  RadioRequestKind kind;
  char             symbol;      // '.' or '-' for Symbol
  EventStamp       stamp;
};

// --- any task -> UI Task ---
enum class UiEventKind : uint8_t {
  Banner,
  SymbolSent,
  SymbolReceived,
  ShowInfo,   // the identity page: serial, node, version, POST, battery
  ShowMain,
  Refresh,    // redraw with fresh POST/battery state, no new symbol
};

struct UiEvent {
  UiEventKind kind;
  char        symbol;     // '.' or '-' for SymbolSent / SymbolReceived
  char        text[20];   // Banner only
  EventStamp  stamp;      // zeroed for Banner
};
