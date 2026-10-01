/*
 * LoRa Mic — INMP441 audio spectrum on the LoRa32 OLED
 * ----------------------------------------------------
 * Reads the INMP441 over I2S at 16 kHz, runs a 512-point FFT (31.25 Hz per bin,
 * 0..8 kHz range) and draws the result like a radio spectrum display.
 *
 * Press the PRG button (GPIO 0) to cycle views:
 *   SPECTRUM  - bar graph, 0..8 kHz left to right, with falling peak markers
 *   WATERFALL - scrolling time/frequency history (newest line at the top)
 *   LEVEL     - big loudness meter (dBFS) + dominant frequency
 * The top line always shows the dominant frequency (Hz) and the level (dBFS).
 *
 * Wiring (INMP441 -> LoRa32):
 *   VDD -> 3V3        GND -> GND
 *   SCK -> GPIO 4     WS  -> GPIO 13    SD -> GPIO 34
 *   L/R -> GND  (mono, left slot — firmware auto-picks the live slot anyway)
 * If your header has no "34", use VP (GPIO 36) or VN (GPIO 39) for SD instead
 * and change MIC_SD below (all three are input-only pins, fine for mic data).
 *
 * Pins deliberately avoided: 5/18/19/23/26/27 (LoRa SPI), 32/33 (LoRa DIO1/DIO2,
 * hard-wired to the radio, not on the header), 21/22 (OLED), 16/17 (embedded
 * flash on the PICO-D4 — driving them bricks boot), 0/2/12/15 (strapping pins),
 * 25 (LED), 35 (battery sense). GPIO 13 is also the SD-card CS: leave the
 * card slot empty while the mic is connected.
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <arduinoFFT.h>
#include <driver/i2s.h>

// --- INMP441 pins ---
constexpr int MIC_SCK = 4;
constexpr int MIC_WS  = 13;
constexpr int MIC_SD  = 34;

// --- On-board OLED ---
constexpr int DISPLAY_SDA = 21;
constexpr int DISPLAY_SCL = 22;
// No reset pin: GPIO 16 is the flash chip-select on this board (see header).
Adafruit_SSD1306 display(128, 64, &Wire, -1);

constexpr int BUTTON_PIN = 0;

// --- Audio / FFT ---
constexpr i2s_port_t I2S_PORT   = I2S_NUM_0;
constexpr uint32_t SAMPLE_RATE  = 16000;
constexpr uint16_t FFT_SIZE     = 512;            // 32 ms per frame
constexpr uint16_t BIN_COUNT    = FFT_SIZE / 2;   // 256 bins, 31.25 Hz each
constexpr float BIN_HZ          = float(SAMPLE_RATE) / FFT_SIZE;

// Display scaling. The graph auto-ranges: its bottom follows the room's noise
// floor, so a quiet room shows faint texture and speech/whistles stand out.
constexpr float DISPLAY_RANGE_DB = 50.0f;   // graph height covers this many dB
constexpr float NOISE_MARGIN_DB  = 4.0f;    // hide this much above the noise floor
constexpr float SILENT_DB        = -150.0f; // "no data" value for a bin
constexpr float PEAK_MIN_DB     = -75.0f;   // below this no "dominant Hz" shown
constexpr float LEVEL_FLOOR_DB  = -70.0f;   // LEVEL view meter range
constexpr float LEVEL_CEILING_DB = 0.0f;

constexpr int HEADER_HEIGHT = 10;
constexpr int GRAPH_HEIGHT  = 64 - HEADER_HEIGHT;  // 54 px
constexpr int GRAPH_WIDTH   = 128;

// Stereo 32-bit frames from the I2S DMA: [L, R, L, R, ...]
int32_t rawFrames[FFT_SIZE * 2];
float vReal[FFT_SIZE];
float vImag[FFT_SIZE];
ArduinoFFT<float> fft(vReal, vImag, FFT_SIZE, float(SAMPLE_RATE));

float columnDb[GRAPH_WIDTH];      // current spectrum, one value per screen column
float columnPeakDb[GRAPH_WIDTH];  // slowly falling peak markers
uint8_t waterfall[GRAPH_HEIGHT][GRAPH_WIDTH];  // 0..255 intensity history
int waterfallTop = 0;             // ring-buffer row holding the newest line

enum class View { Spectrum, Waterfall, Level, Count };
View currentView = View::Spectrum;
const char* const ViewNames[] = {"SPEC", "WFALL", "LEVEL"};

float levelDb = -120.0f;
float dominantHz = 0.0f;
float dominantDb = -120.0f;
int activeSlot = 0;  // 0 = left, 1 = right; picked by signal energy
float noiseFloorDb = -110.0f;     // running estimate of the quiet-room bin level
float displayFloorDb = -106.0f;   // bin level drawn as empty
float displayCeilingDb = -56.0f;  // bin level drawn as full height
// Mic health: a disconnected SD line reads as a constant (0 or -1).
int32_t rawMin = 0, rawMax = 0;
bool isMicAlive = false;

// ---------------------------------------------------------------------------

void setupMicrophone() {
  i2s_config_t config = {};
  config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;  // INMP441: 24 bit in a 32-bit slot
  // Read both slots: the legacy ESP32 driver has a known left/right swap, so
  // rather than guess, capture stereo and use whichever slot carries signal.
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = false;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = MIC_SCK;
  pins.ws_io_num = MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = MIC_SD;

  ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &config, 0, nullptr));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
  i2s_zero_dma_buffer(I2S_PORT);
}

// Fills vReal with one FFT frame of normalised samples (-1..1). Returns RMS dBFS.
float captureFrame() {
  size_t bytesRead = 0;
  i2s_read(I2S_PORT, rawFrames, sizeof(rawFrames), &bytesRead, portMAX_DELAY);
  const size_t frameCount = bytesRead / (2 * sizeof(int32_t));

  // Pick the live slot: an unconnected slot reads as constant 0 or -1.
  double energy[2] = {0, 0};
  for (size_t i = 0; i < frameCount; i++) {
    for (int slot = 0; slot < 2; slot++) {
      const double sample = rawFrames[i * 2 + slot] >> 8;
      energy[slot] += sample * sample;
    }
  }
  activeSlot = energy[1] > energy[0] ? 1 : 0;

  rawMin = INT32_MAX;
  rawMax = INT32_MIN;
  double mean = 0;
  for (size_t i = 0; i < FFT_SIZE; i++) {
    const int32_t sample24 = i < frameCount ? (rawFrames[i * 2 + activeSlot] >> 8) : 0;
    rawMin = min(rawMin, sample24);
    rawMax = max(rawMax, sample24);
    vReal[i] = sample24 / 8388608.0f;  // 2^23
    mean += vReal[i];
  }
  mean /= FFT_SIZE;
  // Real mic noise always wiggles by at least a few LSBs.
  isMicAlive = frameCount > 0 && rawMax - rawMin > 4;

  double sumSquares = 0;
  for (size_t i = 0; i < FFT_SIZE; i++) {
    vReal[i] -= mean;  // INMP441 has a small DC offset
    vImag[i] = 0;
    sumSquares += vReal[i] * vReal[i];
  }
  const float rms = sqrt(sumSquares / FFT_SIZE);
  return 20.0f * log10f(rms + 1e-9f);
}

void analyseSpectrum() {
  fft.windowing(FFTWindow::Hann, FFTDirection::Forward);
  fft.compute(FFTDirection::Forward);
  fft.complexToMagnitude();  // magnitudes now in vReal[0..BIN_COUNT)

  // Hann window halves the amplitude; normalise so a full-scale sine ~ 0 dBFS.
  const float scale = 4.0f / FFT_SIZE;

  // Dominant frequency (skip bins 0-2: DC and rumble below ~90 Hz).
  int bestBin = 3;
  for (int bin = 3; bin < BIN_COUNT; bin++) {
    if (vReal[bin] > vReal[bestBin]) bestBin = bin;
  }
  dominantDb = 20.0f * log10f(vReal[bestBin] * scale + 1e-9f);
  // Parabolic interpolation between neighbouring bins for sub-bin accuracy.
  const float left = vReal[bestBin - 1], centre = vReal[bestBin];
  const float right = bestBin + 1 < BIN_COUNT ? vReal[bestBin + 1] : 0;
  const float denominator = left - 2 * centre + right;
  const float offset = denominator != 0 ? 0.5f * (left - right) / denominator : 0;
  dominantHz = (bestBin + offset) * BIN_HZ;

  // 256 bins -> 128 columns: each column shows the louder of two bins.
  for (int column = 0; column < GRAPH_WIDTH; column++) {
    const float magnitude = max(vReal[column * 2], vReal[column * 2 + 1]);
    const float db = 20.0f * log10f(magnitude * scale + 1e-9f);
    // Fast attack, slower release keeps the bars readable.
    columnDb[column] = db > columnDb[column] ? db : columnDb[column] * 0.7f + db * 0.3f;
    columnPeakDb[column] = max(columnPeakDb[column] - 1.5f, columnDb[column]);
  }
  columnDb[0] = SILENT_DB;  // hide the DC column

  // Noise floor: drops fast when it gets quieter, rises slowly when louder,
  // so a word or whistle doesn't lift the floor but a noisier room does.
  float frameMeanDb = 0;
  for (int column = 2; column < GRAPH_WIDTH; column++) frameMeanDb += columnDb[column];
  frameMeanDb /= GRAPH_WIDTH - 2;
  const float rate = frameMeanDb < noiseFloorDb ? 0.3f : 0.005f;
  noiseFloorDb += rate * (frameMeanDb - noiseFloorDb);
  displayFloorDb = noiseFloorDb + NOISE_MARGIN_DB;
  displayCeilingDb = displayFloorDb + DISPLAY_RANGE_DB;
}

int dbToPixels(float db, float floorDb, float ceilingDb, int height) {
  const float fraction = (db - floorDb) / (ceilingDb - floorDb);
  return constrain(int(fraction * height), 0, height);
}

void drawHeader() {
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (dominantDb > PEAK_MIN_DB) {
    display.printf("%4.0fHz", dominantHz);
  } else {
    display.print("  --Hz");
  }
  display.setCursor(44, 0);
  display.printf("%4.0fdB", levelDb);
  display.setCursor(128 - 6 * strlen(ViewNames[int(currentView)]), 0);
  display.print(ViewNames[int(currentView)]);
  display.drawFastHLine(0, HEADER_HEIGHT - 1, 128, SSD1306_WHITE);
}

void drawSpectrum() {
  const int baseline = 63;
  for (int column = 0; column < GRAPH_WIDTH; column++) {
    const int height = dbToPixels(columnDb[column], displayFloorDb, displayCeilingDb, GRAPH_HEIGHT - 1);
    if (height > 0) display.drawFastVLine(column, baseline - height + 1, height, SSD1306_WHITE);
    const int peak = dbToPixels(columnPeakDb[column], displayFloorDb, displayCeilingDb, GRAPH_HEIGHT - 1);
    if (peak > height + 1) display.drawPixel(column, baseline - peak + 1, SSD1306_WHITE);
  }
  // Tick marks every 1 kHz (32 columns = 1 kHz at 31.25 Hz/bin, 2 bins/column).
  for (int column = 0; column < GRAPH_WIDTH; column += 16) {
    display.drawPixel(column, HEADER_HEIGHT, SSD1306_WHITE);
    if (column % 32 == 0) display.drawPixel(column, HEADER_HEIGHT + 1, SSD1306_WHITE);
  }
}

void pushWaterfallLine() {
  waterfallTop = (waterfallTop + GRAPH_HEIGHT - 1) % GRAPH_HEIGHT;
  for (int column = 0; column < GRAPH_WIDTH; column++) {
    waterfall[waterfallTop][column] =
        dbToPixels(columnDb[column], displayFloorDb, displayCeilingDb, 255);
  }
}

void drawWaterfall() {
  // 4x4 ordered dither turns 0..255 intensity into on/off pixels.
  static const uint8_t Bayer[4][4] = {
      {8, 136, 40, 168}, {200, 72, 232, 104}, {56, 184, 24, 152}, {248, 120, 216, 88}};
  for (int row = 0; row < GRAPH_HEIGHT; row++) {
    const uint8_t* line = waterfall[(waterfallTop + row) % GRAPH_HEIGHT];
    const int y = HEADER_HEIGHT + row;
    for (int column = 0; column < GRAPH_WIDTH; column++) {
      if (line[column] > Bayer[y & 3][column & 3]) display.drawPixel(column, y, SSD1306_WHITE);
    }
  }
}

void drawLevel() {
  display.setTextSize(2);
  display.setCursor(0, 16);
  display.printf("%5.1f dB", levelDb);
  display.setTextSize(1);
  display.setCursor(0, 36);
  if (dominantDb > PEAK_MIN_DB) {
    display.printf("peak %4.0f Hz %4.0fdB", dominantHz, dominantDb);
  } else {
    display.print("peak  -- (quiet)");
  }
  const int width = dbToPixels(levelDb, LEVEL_FLOOR_DB, LEVEL_CEILING_DB, 126);
  display.drawRect(0, 50, 128, 12, SSD1306_WHITE);
  display.fillRect(1, 51, width, 10, SSD1306_WHITE);
}

void drawNoMicWarning() {
  display.setTextSize(1);
  display.setCursor(0, 16);
  display.println("NO MIC DATA");
  display.println("check wiring:");
  display.printf("SCK=%d WS=%d SD=%d\n", MIC_SCK, MIC_WS, MIC_SD);
  display.printf("raw %ld..%ld", long(rawMin), long(rawMax));
}

void pollButton() {
  static bool wasPressed = false;
  static uint32_t lastChangeMs = 0;
  const bool isPressed = digitalRead(BUTTON_PIN) == LOW;
  if (isPressed != wasPressed && millis() - lastChangeMs > 40) {
    lastChangeMs = millis();
    wasPressed = isPressed;
    if (isPressed) {
      currentView = View((int(currentView) + 1) % int(View::Count));
      Serial.printf("View: %s\n", ViewNames[int(currentView)]);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Wire.begin(DISPLAY_SDA, DISPLAY_SCL);
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed.");
  }
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("LoRa Mic");
  display.println("INMP441 starting...");
  display.display();

  for (int column = 0; column < GRAPH_WIDTH; column++) {
    columnDb[column] = columnPeakDb[column] = SILENT_DB;
  }
  setupMicrophone();
  Serial.println("LoRa Mic ready. PRG button cycles SPEC / WFALL / LEVEL.");
}

void loop() {
  pollButton();

  levelDb = captureFrame();
  analyseSpectrum();
  pushWaterfallLine();  // keep history even while another view is shown

  display.clearDisplay();
  drawHeader();
  if (!isMicAlive) {
    drawNoMicWarning();
  } else switch (currentView) {
    case View::Spectrum:  drawSpectrum();  break;
    case View::Waterfall: drawWaterfall(); break;
    default:              drawLevel();     break;
  }
  display.display();

  static uint32_t lastLogMs = 0;
  if (millis() - lastLogMs > 500) {
    lastLogMs = millis();
    Serial.printf("level %6.1f dBFS  peak %5.0f Hz (%6.1f dB)  slot %c  raw %ld..%ld  noise %6.1f dB%s\n",
                  levelDb, dominantHz, dominantDb, activeSlot ? 'R' : 'L',
                  long(rawMin), long(rawMax), noiseFloorDb, isMicAlive ? "" : "  <-- NO MIC DATA");
  }
}
