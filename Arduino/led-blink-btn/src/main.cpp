#include <Arduino.h>

// Blinks a single 7-segment LED segment on and off
// const int segPin = 8;             // segment pin -> Arduino D8 (through 220 ohm resistor)
// const bool COMMON_ANODE = false;  // true if common pin -> 5V, false if common pin -> GND

// void setup() {
//   pinMode(segPin, OUTPUT);
// }

// void loop() {
//   digitalWrite(segPin, COMMON_ANODE ? LOW : HIGH);  // segment ON
//   delay(1000);
//   digitalWrite(segPin, COMMON_ANODE ? HIGH : LOW);  // segment OFF
//   delay(1000);
// }


const int btnPin = 2;
const int ledPin = 8;

bool ledState = false;
bool lastButtonState = HIGH;

void setup() {
  pinMode(btnPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
}

void loop() {
  bool buttonState = digitalRead(btnPin);

  // Detect a new button press (HIGH -> LOW)
  if (lastButtonState == HIGH && buttonState == LOW) {
    ledState = !ledState;              // Toggle the LED
    digitalWrite(ledPin, ledState);
    delay(50);                         // Simple debounce
  }

  lastButtonState = buttonState;
}