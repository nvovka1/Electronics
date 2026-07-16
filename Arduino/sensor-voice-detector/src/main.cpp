#include <Arduino.h>

// put function declarations here:
const int soundAnalogPin = A0;
const int soundDigitalPin = 3;

void setup() {
  Serial.begin(9600);
  pinMode(soundDigitalPin, INPUT);
}

void loop() {
  int analogValue = analogRead(soundAnalogPin);
  int digitalValue = digitalRead(soundDigitalPin);

  Serial.print("Analog level: ");
  Serial.print(analogValue);
  Serial.print("  |  Digital trigger: ");
  Serial.println(digitalValue == HIGH ? "Sound detected!" : "Quiet");

  delay(200);
}