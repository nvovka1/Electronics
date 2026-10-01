/*
 * INMP441 wiring finder (env: pinscan)
 * ------------------------------------
 * Tries every (SCK, WS) pair among the free header pins, clocks the mic, and
 * watches all other candidate pins for toggling data. The pin that toggles only
 * when the mic is clocked is SD. Results go to Serial and the OLED.
 *
 *   pio run -e pinscan -t upload     then read Serial at 115200
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);  // GPIO 16 is flash CS: no reset pin

// Free header pins that can drive a clock, and extra input-only candidates for SD.
const int OutputCandidates[] = {4, 13, 14, 15, 2, 12, 25};
const int InputOnlyCandidates[] = {34, 36, 39};
constexpr int OUTPUT_COUNT = sizeof(OutputCandidates) / sizeof(int);
constexpr int INPUT_ONLY_COUNT = sizeof(InputOnlyCandidates) / sizeof(int);
constexpr int ALL_COUNT = OUTPUT_COUNT + INPUT_ONLY_COUNT;
constexpr int POLL_COUNT = 20000;

int allPins[ALL_COUNT];
int baselineToggles[ALL_COUNT];

void countToggles(int* toggles, int skipA, int skipB) {
  int last[ALL_COUNT];
  for (int i = 0; i < ALL_COUNT; i++) {
    toggles[i] = 0;
    last[i] = digitalRead(allPins[i]);
  }
  for (int poll = 0; poll < POLL_COUNT; poll++) {
    for (int i = 0; i < ALL_COUNT; i++) {
      if (allPins[i] == skipA || allPins[i] == skipB) continue;
      const int level = digitalRead(allPins[i]);
      if (level != last[i]) {
        toggles[i]++;
        last[i] = level;
      }
    }
  }
}

void releasePins() {
  for (int i = 0; i < ALL_COUNT; i++) {
    gpio_reset_pin(gpio_num_t(allPins[i]));
    pinMode(allPins[i], INPUT);
  }
}

bool startClock(int sckPin, int wsPin) {
  i2s_config_t config = {};
  config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = 16000;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.dma_buf_count = 4;
  config.dma_buf_len = 256;
  if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK) return false;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = sckPin;
  pins.ws_io_num = wsPin;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = I2S_PIN_NO_CHANGE;  // we watch SD candidates with digitalRead
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  return true;
}

void show(const char* line1, const char* line2) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("INMP441 pin scan");
  display.println();
  display.println(line1);
  display.println(line2);
  display.display();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(SSD1306_WHITE);
  delay(500);

  for (int i = 0; i < OUTPUT_COUNT; i++) allPins[i] = OutputCandidates[i];
  for (int i = 0; i < INPUT_ONLY_COUNT; i++) allPins[OUTPUT_COUNT + i] = InputOnlyCandidates[i];
  releasePins();

  Serial.println("\n=== INMP441 pin scan ===");
  show("measuring idle...", "");
  countToggles(baselineToggles, -1, -1);
  Serial.print("Idle toggles (no clock):");
  for (int i = 0; i < ALL_COUNT; i++) Serial.printf("  %d:%d", allPins[i], baselineToggles[i]);
  Serial.println();

  int bestSck = -1, bestWs = -1, bestSd = -1, bestScore = 0;
  int toggles[ALL_COUNT];
  for (int s = 0; s < OUTPUT_COUNT; s++) {
    for (int w = 0; w < OUTPUT_COUNT; w++) {
      if (s == w) continue;
      const int sckPin = OutputCandidates[s], wsPin = OutputCandidates[w];
      char line[24];
      snprintf(line, sizeof(line), "SCK=%d WS=%d", sckPin, wsPin);
      show("trying", line);
      releasePins();
      if (!startClock(sckPin, wsPin)) continue;
      delay(350);  // INMP441 wake-up: ~2^18 SCK cycles before data appears
      countToggles(toggles, sckPin, wsPin);
      i2s_driver_uninstall(I2S_NUM_0);

      for (int i = 0; i < ALL_COUNT; i++) {
        if (allPins[i] == sckPin || allPins[i] == wsPin) continue;
        const int score = toggles[i] - baselineToggles[i] * 2;
        if (score > 200) {
          Serial.printf("  SCK=%-2d WS=%-2d -> pin %d toggles %d (idle %d)\n",
                        sckPin, wsPin, allPins[i], toggles[i], baselineToggles[i]);
        }
        if (score > bestScore) {
          bestScore = score;
          bestSck = sckPin;
          bestWs = wsPin;
          bestSd = allPins[i];
        }
      }
    }
  }
  releasePins();

  if (bestScore > 200) {
    char line[24];
    snprintf(line, sizeof(line), "SCK=%d WS=%d SD=%d", bestSck, bestWs, bestSd);
    Serial.printf("RESULT: mic answers on %s (score %d)\n", line, bestScore);
    show("FOUND:", line);
  } else {
    Serial.println("RESULT: no pin toggled when clocked -> mic not powered, dead, or on other pins");
    show("NOT FOUND", "check power/solder");
  }
}

void loop() {}
