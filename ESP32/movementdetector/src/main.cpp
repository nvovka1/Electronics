/*
 * ESP32 Movement Detector
 * ------------------------
 * A PIR motion sensor (HC-SR501) lights an LED and plays the Imperial March
 * on a buzzer, and an I2C OLED (SSD1306, 128x64 @ 0x3C) shows "ON" while
 * motion is active, "OFF" otherwise.
 *
 * Wiring:
 *   PIR OUT   -> GPIO 13
 *   LED (+)   -> GPIO 4  (via 220-330 Ohm resistor, cathode to GND)
 *   Buzzer(+) -> GPIO 5  (PASSIVE buzzer / piezo; - to GND)
 *   OLED SDA  -> GPIO 21     OLED SCL -> GPIO 22
 *   OLED VCC  -> 3.3V        OLED GND -> GND
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "melody.h"

// --- Pin configuration ---
constexpr uint8_t PIR_PIN    = 13;  // PIR sensor output
constexpr uint8_t LED_PIN    = 4;   // LED (external, through a resistor)
constexpr uint8_t BUZZER_PIN = 5;   // Passive buzzer (+ pin); driven by melody.cpp
constexpr uint8_t I2C_SDA    = 21;  // OLED SDA
constexpr uint8_t I2C_SCL    = 22;  // OLED SCL

// --- OLED configuration ---
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C          // use 0x3D if your I2C scan showed that
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);  // -1 = no reset pin

// Keep the LED on for a short "hold" time after the last motion so it does
// not flicker while the sensor's output pulses.
constexpr unsigned long MOTION_HOLD_MS = 2000;

unsigned long lastMotionTime = 0;
bool ledOn = false;

// Show a big centered "ON" / "OFF" on the OLED.
void showState(bool on) {
  const char* text = on ? "ON" : "OFF";
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(4);  // 4x the built-in 6x8 font

  // Center the text using its measured bounds.
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, (SCREEN_HEIGHT - h) / 2);
  display.print(text);
  display.display();  // push buffer to the screen
}

void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // The buzzer pin belongs to the LEDC peripheral from here on - do not
  // digitalWrite() it anywhere, the two would fight over the pin.
  melodyBegin(BUZZER_PIN);

  Wire.begin(I2C_SDA, I2C_SCL);

  Serial.println();
  Serial.println("ESP32 Movement Detector started.");

  // Does the panel acknowledge on the bus at all?
  Wire.beginTransmission(OLED_ADDR);
  Serial.printf("OLED I2C ACK at 0x%02X: %s\n", OLED_ADDR,
                Wire.endTransmission() == 0 ? "YES" : "NO");

  bool ok = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  Serial.printf("display.begin() returned: %s\n", ok ? "true" : "false");

  // DECISIVE TEST: light every pixel straight from the controller (0xA5).
  // This ignores RAM and all drawing code. If the screen does NOT go fully
  // white here, the OLED is not truly powered -> it's VCC/GND wiring.
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(0xFF);
  display.ssd1306_command(SSD1306_DISPLAYALLON);  // 0xA5 - hardware all-on
  Serial.println(">>> Screen must be FULLY WHITE for 4s. If black => OLED VCC/GND wiring.");
  delay(4000);
  display.ssd1306_command(SSD1306_DISPLAYALLON_RESUME);  // 0xA4 - back to RAM

  showState(false);  // start in the OFF state
  Serial.println("Ready. Warming up PIR sensor (~30 s recommended)...");
}

void loop() {
  // Keep the melody moving on every pass - it never blocks.
  melodyUpdate();

  bool motion = digitalRead(PIR_PIN) == HIGH;

  if (motion) {
    lastMotionTime = millis();
    if (!ledOn) {
      ledOn = true;
      digitalWrite(LED_PIN, HIGH);
      melodyStart();  // one full play-through per trigger
      showState(true);
      Serial.println("Motion detected -> ON");
    }
  } else if (ledOn && (millis() - lastMotionTime >= MOTION_HOLD_MS)) {
    ledOn = false;
    digitalWrite(LED_PIN, LOW);
    showState(false);
    // The march is deliberately left to finish rather than being cut off
    // mid-phrase, so it may outlast the "OFF" on the display.
    Serial.println("No motion -> OFF");
  }

  // Heartbeat once per second (confirms the board is alive, not rebooting).
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat >= 1000) {
    lastBeat = millis();
    Serial.printf("alive  motion=%d  ledOn=%d  melody=%d\n", motion, ledOn,
                  melodyIsPlaying());
  }

  // Short poll interval so the 150 ms notes keep their timing.
  delay(5);
}
