// Diagnostic sketch: LCD wiring only.
//
// Talks to nothing but the display -- no GPS, no serial, no libraries beyond
// LiquidCrystal. If this shows text, the LCD half of the project is proven
// good and any remaining fault is on the GPS side.
//
// Build and flash it with:
//   pio run -e lcd-check -t upload
//
// Row 0 prints all 16 column positions so a partially-wired data bus shows up
// as garbage in a specific column range rather than a vague "it looks wrong".
// Row 1 alternates so a frozen display is distinguishable from a live one.

#include <Arduino.h>
#include <LiquidCrystal.h>

constexpr uint8_t LcdRs = 7;
constexpr uint8_t LcdEnable = 8;
constexpr uint8_t LcdD4 = 9;
constexpr uint8_t LcdD5 = 10;
constexpr uint8_t LcdD6 = 11;
constexpr uint8_t LcdD7 = 12;

LiquidCrystal lcd(LcdRs, LcdEnable, LcdD4, LcdD5, LcdD6, LcdD7);

bool isAlternate = false;

void setup() {
  lcd.begin(16, 2);
}

void loop() {
  lcd.setCursor(0, 0);
  lcd.print("0123456789ABCDEF");

  lcd.setCursor(0, 1);
  lcd.print(isAlternate ? "LCD OK  <<<<<<<<" : "LCD OK  >>>>>>>>");
  isAlternate = !isAlternate;

  delay(1000);
}
