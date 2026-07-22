/*
 * LoRa Morse — real telegraph key (two-way)
 * -----------------------------------------
 * The button IS a Morse key:
 *   - hold it  -> LED + buzzer ON (live feedback)
 *   - short press = dot (.)   long press = dash (-)
 *   - pause     -> the keyed dots/dashes decode into a LETTER, sent over LoRa
 *   - long pause-> a space (word gap) is sent
 * The receiving board appends each received character to its screen and beeps it.
 *
 * Both boards run the same firmware; only the node name differs:
 *   pio run -e boardA -t upload      pio run -e boardB -t upload
 *
 * LILYGO/TTGO LoRa32: LoRa + OLED are on-board. Extra part: buzzer + -> GPIO 13.
 * Key button: on-board PRG button (GPIO 0), or wire your own between GPIO 0 and GND.
 * ALWAYS attach the antenna before powering.
 */

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#ifndef NODE_NAME
  #define NODE_NAME "?"
#endif

// --- On-board LoRa pins (board macros; fallbacks for LILYGO LoRa32) ---
#ifndef LORA_SCK
  #define LORA_SCK  5
  #define LORA_MISO 19
  #define LORA_MOSI 27
  #define LORA_CS   18
  #define LORA_RST  23
  #define LORA_IRQ  26
#endif
constexpr long LORA_FREQ = 868E6;   // must match both boards + your hardware

// --- On-board OLED pins ---
#ifndef OLED_SDA
  #define OLED_SDA 21
#endif
#ifndef OLED_SCL
  #define OLED_SCL 22
#endif
#ifndef OLED_RST
  #define OLED_RST 16
#endif
Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);

// --- I/O pins ---
constexpr uint8_t LED_PIN    = 25;  // on-board LED (sidetone)
constexpr uint8_t BUZZER_PIN = 13;  // external active buzzer (- to GND)
constexpr uint8_t KEY_PIN    = 0;   // Morse key button (pressed = LOW)

// --- Morse key timing (tune to your hand) ---
constexpr unsigned long DASH_MIN_MS   = 300;   // press >= this = dash, else dot
constexpr unsigned long LETTER_GAP_MS = 900;   // silence this long = end of letter
constexpr unsigned long WORD_GAP_MS   = 2200;  // silence this long = word space
constexpr unsigned long DEBOUNCE_MS   = 15;

// After this many RECEIVED characters, the RX line clears and starts over.
constexpr int RX_CLEAR_AFTER = 5;

// Morse table (also used to beep received characters).
const char* morseFor(char c) {
  switch (toupper((unsigned char)c)) {
    case 'A': return ".-";    case 'B': return "-...";  case 'C': return "-.-.";
    case 'D': return "-..";   case 'E': return ".";     case 'F': return "..-.";
    case 'G': return "--.";   case 'H': return "....";  case 'I': return "..";
    case 'J': return ".---";  case 'K': return "-.-";   case 'L': return ".-..";
    case 'M': return "--";    case 'N': return "-.";    case 'O': return "---";
    case 'P': return ".--.";  case 'Q': return "--.-";  case 'R': return ".-.";
    case 'S': return "...";   case 'T': return "-";     case 'U': return "..-";
    case 'V': return "...-";  case 'W': return ".--";   case 'X': return "-..-";
    case 'Y': return "-.--";  case 'Z': return "--..";
    case '0': return "-----"; case '1': return ".----"; case '2': return "..---";
    case '3': return "...--"; case '4': return "....-"; case '5': return ".....";
    case '6': return "-...."; case '7': return "--..."; case '8': return "---..";
    case '9': return "----.";
    default:  return "";
  }
}

// Decode a "..-" style symbol string into a character (0 if unknown).
char decodeMorse(const String& sym) {
  for (char c = 'A'; c <= 'Z'; c++) if (sym == morseFor(c)) return c;
  for (char c = '0'; c <= '9'; c++) if (sym == morseFor(c)) return c;
  return 0;
}

// --- Conversation state ---
String txText = "";    // what we have keyed and sent
String rxText = "";    // what we have received (cleared every RX_CLEAR_AFTER chars)
String symbol = "";    // dots/dashes of the letter currently being keyed
int    rxCount = 0;    // received characters shown since the last clear

void drawScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);
  display.setCursor(0, 0);  display.print("Node "); display.print(NODE_NAME);
  display.setCursor(0, 16); display.print("TX:");    display.print(txText);
  display.setCursor(0, 30); display.print("RX:");    display.print(rxText);
  display.setCursor(0, 50); display.print("key:");   display.print(symbol);
  display.display();
}

void appendTrimmed(String& dst, char c, int maxLen = 17) {
  dst += c;
  if ((int)dst.length() > maxLen) dst = dst.substring(dst.length() - maxLen);
}

// Beep a single received character as Morse on LED + buzzer.
void beepChar(char c) {
  constexpr int U = 120;
  for (const char* p = morseFor(c); *p; p++) {
    digitalWrite(LED_PIN, HIGH); digitalWrite(BUZZER_PIN, HIGH);
    delay(*p == '.' ? U : U * 3);
    digitalWrite(LED_PIN, LOW);  digitalWrite(BUZZER_PIN, LOW);
    delay(U);
  }
}

// Transmit one character over LoRa, then go back to listening.
void sendChar(char c) {
  LoRa.beginPacket();
  LoRa.write((uint8_t)c);
  LoRa.endPacket();
  LoRa.receive();
  appendTrimmed(txText, c);
  Serial.printf("TX char: '%c'\n", c);
  drawScreen();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(KEY_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    Serial.println("OLED init failed.");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init FAILED - check pins / antenna.");
    display.clearDisplay(); display.setCursor(0, 0);
    display.print("LoRa FAIL"); display.display();
    for (;;) delay(1000);
  }
  LoRa.receive();

  drawScreen();
  Serial.printf("Node %s ready. Key Morse with the button.\n", NODE_NAME);
}

void loop() {
  unsigned long nowMs = millis();

  // ---------- RECEIVE ----------
  int packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    while (LoRa.available()) {
      char c = (char)LoRa.read();
      if (rxCount >= RX_CLEAR_AFTER) {  // reached the limit -> clear and restart
        rxText = "";
        rxCount = 0;
      }
      rxText += c;
      rxCount++;
      Serial.printf("RX char: '%c'  (RSSI %d)\n", c, LoRa.packetRssi());
      drawScreen();
      if (c != ' ') beepChar(c);   // play the received letter
    }
    LoRa.receive();
  }

  // ---------- MORSE KEY (button) ----------
  static bool keyDown = false;
  static unsigned long pressStart = 0, lastReleaseTime = 0, lastEdge = 0;
  static bool letterPending = false, wordSpaceSent = true;

  bool pressed = (digitalRead(KEY_PIN) == LOW);

  // Live sidetone: LED + buzzer follow the key exactly.
  digitalWrite(LED_PIN, pressed ? HIGH : LOW);
  digitalWrite(BUZZER_PIN, pressed ? HIGH : LOW);

  // Key pressed (with debounce)
  if (pressed && !keyDown && (nowMs - lastEdge) > DEBOUNCE_MS) {
    keyDown = true;
    pressStart = nowMs;
    lastEdge = nowMs;
  }

  // Key released -> record dot or dash
  if (!pressed && keyDown && (nowMs - lastEdge) > DEBOUNCE_MS) {
    keyDown = false;
    lastEdge = nowMs;
    unsigned long dur = nowMs - pressStart;
    symbol += (dur < DASH_MIN_MS) ? '.' : '-';
    lastReleaseTime = nowMs;
    letterPending = true;
    wordSpaceSent = false;
    drawScreen();
  }

  // End of letter -> decode and send it
  if (letterPending && !keyDown && (nowMs - lastReleaseTime) > LETTER_GAP_MS) {
    char c = decodeMorse(symbol);
    if (c) sendChar(c);
    else   Serial.println("Unknown symbol: " + symbol);
    symbol = "";
    letterPending = false;
    drawScreen();
  }

  // Word gap -> send a single space
  if (!letterPending && symbol.length() == 0 && !keyDown && !wordSpaceSent &&
      txText.length() > 0 && txText[txText.length() - 1] != ' ' &&
      (nowMs - lastReleaseTime) > WORD_GAP_MS) {
    sendChar(' ');
    wordSpaceSent = true;
  }
}
