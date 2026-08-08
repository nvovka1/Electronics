// Everything drawn on the 128x64 OLED.
//
// Owns only presentation state — which page is showing, which settings row is
// selected, whether a dialog is up. It reads from radio and battery but never
// commands them; main.cpp translates joystick events into radio calls.

#pragma once

#include <Arduino.h>

namespace ui {

enum class Page : uint8_t {
    Live,
    Packet,
    Signal,
    Sweep,
    Settings,
    Count,
};

enum class Dialog : uint8_t {
    None,
    SelfTestConfirm,
    SelfTestPassed,
    SelfTestFailed,
};

// Settings rows. Stick is read-only, for diagnosing joystick wiring in the field.
enum class SettingsRow : uint8_t {
    Preset,
    SpreadingFactor,
    Bandwidth,
    CodingRate,
    SyncWord,
    StepSize,
    Stick,
    Count,
};

// Returns false if the SSD1306 never answered on I2C.
bool begin();

void showSplash(bool radioOk, bool joystickOk);

void changePage(int delta);
Page page();

void moveSettingsRow(int delta);
SettingsRow settingsRow();

void toggleHexEmphasis();

void showDialog(Dialog dialog);
Dialog dialog();

// Rate-limited internally; call as often as you like.
void render();

}  // namespace ui
