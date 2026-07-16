/*
 * ESP32 Movement Detector
 * ------------------------
 * A PIR motion sensor (HC-SR501) drives an LED + buzzer, and an I2C OLED
 * (SSD1306, 128x64) shows "ON" while motion is active, "OFF" otherwise.
 *
 * Wiring:
 *   PIR OUT   -> GPIO 13
 *   LED (+)   -> GPIO 4  (via 220-330 Ohm resistor, cathode to GND)
 *   Buzzer(+) -> GPIO 5  (active buzzer; - to GND)
 *   OLED SDA  -> GPIO 21     OLED SCL -> GPIO 22
 *   OLED VCC  -> 3.3V        OLED GND -> GND
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- Pin configuration ---
constexpr uint8_t PIR_PIN    = 13;  // PIR sensor output
constexpr uint8_t LED_PIN    = 4;   // LED (external, through a resistor)
constexpr uint8_t BUZZER_PIN = 5;   // Active buzzer (+ pin); sounds while ON
constexpr uint8_t I2C_SDA    = 21;  // OLED SDA
constexpr uint8_t I2C_SCL    = 22;  // OLED SCL

// --- OLED configuration ---
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDR     0x3C          // most SSD1306 modules; some use 0x3D
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
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(I2C_SDA, I2C_SCL);

  // Init the OLED; fall back to the alternate address if the module uses 0x3D.
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR) &&
      !display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
    Serial.println("SSD1306 not found - check wiring/address.");
  }

  showState(false);  // start in the OFF state
  Serial.println("ESP32 Movement Detector ready.");
}

void loop() {
  bool motion = digitalRead(PIR_PIN) == HIGH;

  if (motion) {
    lastMotionTime = millis();
    if (!ledOn) {
      ledOn = true;
      digitalWrite(LED_PIN, HIGH);
      digitalWrite(BUZZER_PIN, HIGH);
      showState(true);
      Serial.println("Motion detected -> ON");
    }
  } else if (ledOn && (millis() - lastMotionTime >= MOTION_HOLD_MS)) {
    ledOn = false;
    digitalWrite(LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    showState(false);
    Serial.println("No motion -> OFF");
  }

  delay(50);
}
