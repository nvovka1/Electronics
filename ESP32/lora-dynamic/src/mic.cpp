/*
 * LoRa Dynamic — mic (sender)
 * ---------------------------
 * Reads the INMP441 over I2S at 16 kHz and sends the audio as UDP packets to
 * the speaker board - whichever board last broadcast a hello (see config.h).
 * With USE_LORA it sends Codec2-compressed speech over the LoRa radio instead.
 *
 * Wiring (INMP441 -> LoRa32), same as the lora-mic project:
 *   VDD -> 3V3        GND -> GND
 *   SCK -> GPIO 4     WS  -> GPIO 13    SD -> GPIO 34
 *   L/R -> GND
 *
 * Every packet this board transmits puts a burst of noise into the mic (it
 * rides on the supply, TX power makes no difference). Fewer, bigger packets
 * mean less noise; a 100 uF + 100 nF capacitor across the mic's VDD/GND pins
 * is the hardware fix.
 *
 * The OLED shows what it is doing, so it can run from a phone charger.
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <driver/i2s.h>
#include <soc/rtc_cntl_reg.h>
#include <LoRa.h>
#include <codec2.h>
#include "config.h"
#include "spectrum.h"
#include "battery.h"
#include "lora_radio.h"

// Codec2 needs a lot of stack; with the default 8 KB loop() crashes.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

constexpr int MIC_SCK = 4;
constexpr int MIC_WS  = 13;
constexpr int MIC_SD  = 34;

// Speech from the INMP441 is quiet; raise it before it reaches the 8-bit DAC.
// 16 made a loud voice clip. Over LoRa 8 clipped too (rms up to 20000, peaks
// at full scale), and Codec2 turns a clipped voice into mush.
constexpr int GAIN = USE_LORA ? 2 : 8;

// High-pass filter at ~200 Hz: removes hum/rumble and the mic's DC offset,
// keeps the voice band.
constexpr float HIGH_PASS = 0.927f;  // RC / (RC + 1/16000), RC = 1 / (2 pi 200 Hz)

// Noise gate: a packet quieter than this (rms) is sent as silence, unless
// someone spoke in the last GATE_HOLD_MS. Measured: WiFi clicks ~1250, voice 4000+.
// LoRa has no WiFi clicks: a quiet room is ~130, normal talking ~250-1500.
constexpr int GATE_RMS = USE_LORA ? 400 : 2500;
constexpr uint32_t GATE_HOLD_MS = 300;

// No reset pin: GPIO 16 is the flash chip-select on the PICO-D4.
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Spectrum spectrum;

WiFiUDP udp;
WiFiUDP hello;  // listens for the speaker's hello broadcasts
IPAddress speakerIp;
int32_t rawFrames[SAMPLES_PER_PACKET * 2];  // stereo 32-bit: [L, R, L, R, ...]
int16_t samples[SAMPLES_PER_PACKET];        // filtered audio, -32768..32767
uint8_t packet[SAMPLES_PER_PACKET];         // what is sent: 8-bit, 128 = silence

// LoRa mode: 8 kHz speech is collected into 20 ms frames, each compressed to
// 8 bytes, and CODEC2_FRAMES_PER_PACKET of them go out in one LoRa packet.
CODEC2* codec2 = nullptr;
int16_t codecFrame[CODEC2_SAMPLES_PER_FRAME];
size_t codecFrameFill = 0;
uint8_t loraPacket[LORA_PACKET_SIZE];
size_t loraPacketFrames = 0;
uint32_t loraSent = 0, loraBusy = 0, encodeMaxUs = 0;  // for logLevel()

void setupMicrophone() {
  i2s_config_t config = {};
  config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;  // INMP441: 24 bit in a 32-bit slot
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;  // read both, use the live one
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;   // 8 x 256 frames = 128 ms, two packets

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = MIC_SCK;
  pins.ws_io_num = MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = MIC_SD;

  ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &config, 0, nullptr));
  ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, &pins));
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// Shows up to three lines on the OLED and echoes them to Serial.
void showStatus(const String& line1, const String& line2 = "", const String& line3 = "") {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("LoRa Dynamic - MIC");
  display.println();
  display.println(line1);
  display.println(line2);
  display.println(line3);
  display.display();
  Serial.printf("%s | %s | %s\n", line1.c_str(), line2.c_str(), line3.c_str());
}

// Follows the speaker's hello broadcasts; a new speaker IP takes over at once.
void checkHello() {
  if (hello.parsePacket() == 0) return;
  char text[32] = {};
  hello.read(text, sizeof(text) - 1);
  if (strcmp(text, HELLO_TEXT) != 0 || hello.remoteIP() == speakerIp) return;
  speakerIp = hello.remoteIP();
  showStatus("IP " + WiFi.localIP().toString(), "Streaming to", speakerIp.toString());
}

// LoRa mode: no network to join - the mic just transmits, the speaker listens.
void startLora(const String& lastReset) {
  codec2 = codec2_create(CODEC2_MODE_3200);
  if (!startLoraRadio()) {
    showStatus("LoRa radio not found", lastReset);
    while (true) delay(1000);
  }
  showStatus("LoRa " + String(LORA_FREQUENCY_HZ / 1e6, 3) + " MHz", "Sending", lastReset);
}

// WiFi mode: join the network, then wait for the speaker's hello.
void startWifi(const String& lastReset) {
  showStatus("Connecting to " + String(WIFI_SSID), lastReset);
  // WiFi's radio start-up dips the supply for a moment and the brownout
  // detector resets the board; switch it off just for that moment.
  const uint32_t brownoutConfig = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  delay(1000);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, brownoutConfig);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  WiFi.setSleep(false);

  hello.begin(HELLO_PORT);
  showStatus("IP " + WiFi.localIP().toString(), "Waiting for speaker");
  while (speakerIp == IPAddress()) {
    checkHello();
    delay(50);
  }
}

void setup() {
  // WiFi: 80 MHz draws less current, so WiFi start-up doesn't trip the
  // brownout reset. LoRa has no WiFi, and Codec2 needs the full 240 MHz.
  setCpuFrequencyMhz(USE_LORA ? 240 : 80);
  Serial.begin(115200);

  Wire.begin(21, 22);
  Wire.setClock(400000);  // a full screen update then takes ~25 ms of the 64 ms per packet
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(SSD1306_WHITE);
  // If this says "brownout", the power supply sags when WiFi starts.
  const String lastReset = esp_reset_reason() == ESP_RST_BROWNOUT ? "Last reset: BROWNOUT" : "";

  setupMicrophone();
  batteryBegin();
  if (USE_LORA) startLora(lastReset);
  else startWifi(lastReset);
}

// Reads one packet of audio into `samples`.
void readSamples() {
  size_t bytesRead = 0;
  i2s_read(I2S_NUM_0, rawFrames, sizeof(rawFrames), &bytesRead, portMAX_DELAY);

  // The unconnected slot reads constant 0 or -1; take the one with energy.
  double energy[2] = {0, 0};
  for (size_t i = 0; i < SAMPLES_PER_PACKET; i++) {
    for (int slot = 0; slot < 2; slot++) {
      const double sample = rawFrames[i * 2 + slot] >> 8;
      energy[slot] += sample * sample;
    }
  }
  const int slot = energy[1] > energy[0] ? 1 : 0;

  // 24-bit -> 16-bit, high-pass filtered, with gain.
  static float previousSample = 0, filtered = 0;
  for (size_t i = 0; i < SAMPLES_PER_PACKET; i++) {
    const float sample = rawFrames[i * 2 + slot] >> 16;  // top 16 bits
    filtered = HIGH_PASS * (filtered + sample - previousSample);
    previousSample = sample;
    samples[i] = int16_t(constrain(filtered * GAIN, -32768.0f, 32767.0f));
  }
  // Robot voice: multiply by a 400 Hz sine (ring modulation).
  // 30-50 Hz is only a subtle flutter; 300-600 Hz sounds clearly metallic.
  // Not over LoRa: Codec2 rebuilds speech from the voice's pitch, and ring
  // modulation destroys exactly that, so it came out as noise.
  if (USE_LORA) return;
  static float phase = 0;
  for (size_t i = 0; i < SAMPLES_PER_PACKET; i++) {
    samples[i] = int16_t(samples[i] * sinf(phase));
    phase += 2 * PI * 400 / SAMPLE_RATE;
    if (phase > 2 * PI) phase -= 2 * PI;
  }
}

// Every 2 s prints the level sent: rms/peak 0..32767 (one DAC step is 256).
void logLevel() {
  static double sumSquares = 0;
  static int peak = 0;
  static uint32_t sampleCount = 0, startMs = 0;
  for (size_t i = 0; i < SAMPLES_PER_PACKET; i++) {
    sumSquares += double(samples[i]) * samples[i];
    peak = max(peak, abs(int(samples[i])));
  }
  sampleCount += SAMPLES_PER_PACKET;
  if (millis() - startMs < 2000) return;
  Serial.printf("sent rms %d  peak %d", int(sqrt(sumSquares / sampleCount)), peak);
  // LoRa, per 2 s: ~12 packets while talking; busy > 0 means packets were
  // lost, encode > 20000 us (one frame's length) means the mic can't keep up.
  if (USE_LORA) Serial.printf("  lora sent %lu busy %lu  encode max %lu us", loraSent, loraBusy, encodeMaxUs);
  Serial.println();
  loraSent = loraBusy = encodeMaxUs = 0;
  sumSquares = 0;
  peak = 0;
  sampleCount = 0;
  startMs = millis();
}

// True while someone is speaking (or did in the last GATE_HOLD_MS).
bool isVoice() {
  double sumSquares = 0;
  for (size_t i = 0; i < SAMPLES_PER_PACKET; i++) sumSquares += double(samples[i]) * samples[i];
  static uint32_t lastVoiceMs = 0;
  if (sqrt(sumSquares / SAMPLES_PER_PACKET) > GATE_RMS) lastVoiceMs = millis();
  return millis() - lastVoiceMs < GATE_HOLD_MS;
}

void sendUdp(bool isOpen) {
  checkHello();
  for (size_t i = 0; i < SAMPLES_PER_PACKET; i++) {
    packet[i] = isOpen ? uint8_t((samples[i] >> 8) + 128) : 128;
  }
  udp.beginPacket(speakerIp, AUDIO_PORT);
  udp.write(packet, sizeof(packet));
  udp.endPacket();
}

// 16 kHz -> 8 kHz (the average of each pair, which also cuts what 8 kHz can't
// carry), compressed 20 ms at a time; a full packet goes out only while
// someone speaks, so the radio is off in silence.
void sendLora(bool isOpen) {
  for (size_t i = 0; i < SAMPLES_PER_PACKET; i += 2) {
    codecFrame[codecFrameFill++] = int16_t((samples[i] + samples[i + 1]) / 2);
    if (codecFrameFill < CODEC2_SAMPLES_PER_FRAME) continue;
    codecFrameFill = 0;

    const uint32_t startUs = micros();
    codec2_encode(codec2, loraPacket + loraPacketFrames * CODEC2_BYTES_PER_FRAME, codecFrame);
    encodeMaxUs = max(encodeMaxUs, uint32_t(micros() - startUs));
    if (++loraPacketFrames < CODEC2_FRAMES_PER_PACKET) continue;
    loraPacketFrames = 0;

    if (!isOpen) continue;
    if (LoRa.beginPacket() == 0) {  // the last packet is still on air: this one is lost
      loraBusy++;
      continue;
    }
    LoRa.write(loraPacket, sizeof(loraPacket));
    LoRa.endPacket(true);  // true: don't wait the ~120 ms on air, keep reading the mic
    loraSent++;
  }
}

void loop() {
  readSamples();
  logLevel();

  const bool isOpen = isVoice();
  if (USE_LORA) sendLora(isOpen);
  else sendUdp(isOpen);

  spectrum.update(samples, SAMPLES_PER_PACKET);
  const String target = USE_LORA ? String("LoRa") : "." + String(speakerIp[3]);
  drawSpectrumScreen(display, spectrum, "MIC -> " + target + "  bat " + getBatteryPercent() + "%");
}
