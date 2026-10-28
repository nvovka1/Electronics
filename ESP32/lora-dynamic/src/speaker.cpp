/*
 * LoRa Dynamic — speaker (receiver)
 * ---------------------------------
 * Joins WiFi, says hello to the mic board every 2 s (UDP broadcast), receives
 * its audio packets and plays them on the ESP32's built-in DAC. The OLED shows
 * what it is doing. With USE_LORA it listens on the LoRa radio instead and
 * decodes the mic's Codec2 speech.
 *
 * Wiring (SC8002B S/V/G module -> LoRa32):
 *   V -> 5V     G -> GND    S -> GPIO 25 (DAC1)
 * Not V -> 3V3: the amp overloaded the board's 3.3 V regulator (it got very
 * hot and the board stopped answering on USB, so it could not be flashed).
 * Only GPIO 25 and 26 have a DAC, and 26 is wired to the LoRa radio, so S must
 * be on 25 — GPIO 2 has no DAC (and is a boot strapping pin).
 * GPIO 25 is also the on-board LED: it will glow with the sound, that's fine.
 * If it is too loud or distorts, put a 10k potentiometer between GPIO 25 and IN.
 */

#include <Arduino.h>
#include <LoRa.h>
#include <codec2.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <driver/i2s.h>
#include <soc/rtc_cntl_reg.h>
#include "config.h"
#include "spectrum.h"
#include "battery.h"
#include "lora_radio.h"

// Codec2 needs a lot of stack; with the default 8 KB loop() crashes.
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

// No reset pin: GPIO 16 is the flash chip-select on the PICO-D4.
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Spectrum spectrum;
int16_t received[SAMPLES_PER_PACKET];  // the packet as -32768..32767, for the spectrum bars

WiFiUDP udp;
uint8_t packet[SAMPLES_PER_PACKET];
uint16_t dacFrames[SAMPLES_PER_PACKET * 2];  // stereo: same sample on both channels

// LoRa mode: each packet holds CODEC2_FRAMES_PER_PACKET compressed 20 ms frames.
CODEC2* codec2 = nullptr;
uint8_t loraPacket[LORA_PACKET_SIZE];
int16_t codecFrame[CODEC2_SAMPLES_PER_FRAME];       // one decoded frame, 8 kHz
int16_t upsampled[CODEC2_SAMPLES_PER_FRAME * 2];    // the same frame at the DAC's 16 kHz

uint32_t packetCount = 0, lastPacketMs = 0;
uint32_t playEndMs = 0;  // when the audio queued for the DAC runs out

void setupDac() {
  i2s_config_t config = {};
  config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN);
  config.sample_rate = SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_MSB;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 16;
  config.dma_buf_len = 256;     // 16 x 256 frames = 256 ms: four packets against WiFi jitter
  config.tx_desc_auto_clear = true;  // silence, not a buzzing loop, when packets stop
  // Must be on: with the default clock the built-in DAC played ~5.5x too fast
  // (asked 16000, measured 89000 samples/s) - chipmunk voice and clicks.
  config.use_apll = true;

  ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &config, 0, nullptr));
  i2s_set_pin(I2S_NUM_0, nullptr);                // built-in DAC
  i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);     // GPIO 25 only (26 is LoRa DIO0)
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// Half a second of 1 kHz at start-up: if you hear it, DAC, amp and speaker work.
void beep() {
  for (size_t i = 0; i < SAMPLES_PER_PACKET; i++) {
    const float wave = sinf(2 * PI * 1000 * i / SAMPLE_RATE);  // 1024 samples = 64 whole periods
    dacFrames[i * 2] = dacFrames[i * 2 + 1] = uint16_t((128 + 100 * wave)) << 8;
  }
  size_t written = 0;
  for (int repeat = 0; repeat < 8; repeat++) {
    i2s_write(I2S_NUM_0, dacFrames, sizeof(dacFrames), &written, portMAX_DELAY);
  }
}

// Shows up to three lines on the OLED and echoes them to Serial.
void showStatus(const String& line1, const String& line2 = "", const String& line3 = "") {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("LoRa Dynamic - SPEAKER");
  display.println();
  display.println(line1);
  display.println(line2);
  display.println(line3);
  display.display();
  Serial.printf("%s | %s | %s\n", line1.c_str(), line2.c_str(), line3.c_str());
}

void showWaiting() {
  if (USE_LORA) showStatus("LoRa " + String(LORA_FREQUENCY_HZ / 1e6, 3) + " MHz", "Waiting for mic");
  else showStatus("IP " + WiFi.localIP().toString(), "Waiting for mic");
}

// LoRa mode: nothing to join, just listen.
void startLora(const String& lastReset) {
  codec2 = codec2_create(CODEC2_MODE_3200);
  if (!startLoraRadio()) {
    showStatus("LoRa radio not found", lastReset);
    while (true) delay(1000);
  }
}

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
  WiFi.setSleep(false);  // power save delays packets and makes the sound choppy

  udp.begin(AUDIO_PORT);
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

  setupDac();
  showStatus("Beep...");
  beep();
  batteryBegin();
  if (USE_LORA) startLora(lastReset);
  else startWifi(lastReset);
  showWaiting();
}

// Tells the mic board where to send its audio.
void sayHello() {
  static uint32_t lastHelloMs = 0;
  if (lastHelloMs != 0 && millis() - lastHelloMs < HELLO_PERIOD_MS) return;
  lastHelloMs = millis();
  udp.beginPacket(WiFi.broadcastIP(), HELLO_PORT);
  udp.print(HELLO_TEXT);
  udp.endPacket();
}

// Plays `count` 16 kHz samples (-32768..32767) on the DAC.
void play(const int16_t* samples, size_t count) {
  // The DAC takes the high byte of each 16-bit word: 0 -> 128 << 8 = silence.
  for (size_t i = 0; i < count; i++) {
    dacFrames[i * 2] = dacFrames[i * 2 + 1] = uint16_t(samples[i] + 32768);
  }
  size_t written = 0;
  i2s_write(I2S_NUM_0, dacFrames, count * 2 * sizeof(dacFrames[0]), &written, portMAX_DELAY);
  playEndMs = max(playEndMs, uint32_t(millis())) + count * 1000 / SAMPLE_RATE;
}

// Keeps ~20 ms of silence queued between packets. Left to run dry, the I2S
// driver sends 0, which the DAC plays as 0 V instead of its 128 mid-level:
// a jump down and back up, heard as a click at every gap.
void fillGaps() {
  static const int16_t silence[256] = {};  // 16 ms
  while (int32_t(playEndMs - millis()) < 20) play(silence, 256);
}

void receiveUdp() {
  sayHello();
  if (udp.parsePacket() != sizeof(packet)) return;
  udp.read(packet, sizeof(packet));

  for (size_t i = 0; i < SAMPLES_PER_PACKET; i++) received[i] = int16_t((packet[i] - 128) << 8);
  play(received, SAMPLES_PER_PACKET);
  packetCount++;
  lastPacketMs = millis();

  spectrum.update(received, SAMPLES_PER_PACKET);
  drawSpectrumScreen(display, spectrum, "SPK <- ." + String(udp.remoteIP()[3]) + "  bat " + getBatteryPercent() + "%");
}

void receiveLora() {
  if (LoRa.parsePacket() != sizeof(loraPacket)) return;
  LoRa.readBytes(loraPacket, sizeof(loraPacket));
  // parsePacket() put the radio to sleep, and the next packet starts ~10 ms
  // after this one ended - long before decoding below is done. Without this
  // second call (which starts listening again) every other packet was lost.
  LoRa.parsePacket();

  // Packets come every 128-192 ms (the mic sends between mic reads), each
  // holding 160 ms. Start a new sentence 128 ms behind, so a late packet
  // doesn't leave a gap.
  if (millis() - lastPacketMs > 500) {
    static int16_t silence[SAMPLES_PER_PACKET] = {};
    play(silence, SAMPLES_PER_PACKET);
    play(silence, SAMPLES_PER_PACKET);
  }
  packetCount++;
  lastPacketMs = millis();

  for (size_t frame = 0; frame < CODEC2_FRAMES_PER_PACKET; frame++) {
    codec2_decode(codec2, codecFrame, loraPacket + frame * CODEC2_BYTES_PER_FRAME);
    // 8 kHz -> 16 kHz: a new sample halfway between each pair.
    static int16_t previous = 0;
    for (size_t i = 0; i < CODEC2_SAMPLES_PER_FRAME; i++) {
      upsampled[i * 2] = int16_t((previous + codecFrame[i]) / 2);
      upsampled[i * 2 + 1] = codecFrame[i];
      previous = codecFrame[i];
    }
    play(upsampled, CODEC2_SAMPLES_PER_FRAME * 2);
  }

  // The bars show the last frame; above 4 kHz stays empty, Codec2 doesn't carry it.
  spectrum.update(upsampled, CODEC2_SAMPLES_PER_FRAME * 2);
  drawSpectrumScreen(display, spectrum, "SPK " + String(LoRa.packetRssi()) + "dBm  bat " + getBatteryPercent() + "%");
}

void loop() {
  fillGaps();
  if (USE_LORA) receiveLora();
  else receiveUdp();

  // No audio for a while: back to the status screen.
  static uint32_t lastStatusMs = 0;
  const bool isPlaying = lastPacketMs != 0 && millis() - lastPacketMs < 1000;
  if (!isPlaying && millis() - lastStatusMs > 2000) {
    lastStatusMs = millis();
    showWaiting();
  }
  if (isPlaying && millis() - lastStatusMs > 2000) {
    lastStatusMs = millis();
    if (USE_LORA) Serial.printf("packets %lu, signal %d dBm\n", packetCount, LoRa.packetRssi());
    else Serial.printf("packets %lu from %s\n", packetCount, udp.remoteIP().toString().c_str());
  }
}
