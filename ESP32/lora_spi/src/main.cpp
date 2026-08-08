#include <Arduino.h>

constexpr uint8_t PIN_SCK  = 5;
constexpr uint8_t PIN_MISO = 19;
constexpr uint8_t PIN_MOSI = 27;
constexpr uint8_t PIN_CS   = 18;
constexpr uint8_t PIN_RST  = 23;

constexpr uint8_t REG_VERSION = 0x42;
constexpr uint8_t EXPECTED    = 0x12;

void startCommunication();

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("Start");

  pinMode(PIN_MOSI, OUTPUT);
  pinMode(PIN_MISO, INPUT);
  pinMode(PIN_SCK, OUTPUT);
  pinMode(PIN_CS, OUTPUT);

  digitalWrite(PIN_SCK, LOW); 
  digitalWrite(PIN_CS, HIGH); 
}

void loop() {
  startCommunication();
  delay(2000);
}


void resetRadio() {
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(10);
}

void startCommunication() {

digitalWrite(PIN_CS, LOW); 

   //send request
  for (int8_t bit = 7; bit >= 0; --bit) {
      digitalWrite(PIN_MOSI, (REG_VERSION >> bit) & 0x01);
      digitalWrite(PIN_SCK, HIGH);
      digitalWrite(PIN_SCK, LOW);
    }

  // read response
    uint8_t response = 0;
    for (int8_t bit = 7; bit >= 0; --bit) { 
      digitalWrite(PIN_SCK, HIGH);
      response = (uint8_t)((response << 1) | (digitalRead(PIN_MISO) ? 1 : 0));
      digitalWrite(PIN_SCK, LOW);
    }

  digitalWrite(PIN_CS, HIGH);

  Serial.printf("0x42 = 0x%02X  (0b", response);
  for (int8_t bit = 7; bit >= 0; --bit) {
    Serial.print((response >> bit) & 1);
  }

  Serial.print(')');

  if (response == EXPECTED)    
    Serial.println("  OK - SX1276");
}