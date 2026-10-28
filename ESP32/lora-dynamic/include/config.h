#pragma once

// Shared by both boards.
constexpr const char* WIFI_SSID     = "BBC";
constexpr const char* WIFI_PASSWORD = "liza2017";

// The speaker broadcasts HELLO_TEXT to HELLO_PORT every 2 s; the mic sends its
// audio to whoever said hello last, so either board can restart or be swapped.
constexpr uint16_t AUDIO_PORT       = 5005;  // UDP, mic -> speaker
constexpr uint16_t HELLO_PORT       = 5006;  // UDP broadcast, speaker -> mic
constexpr const char* HELLO_TEXT    = "lora-dynamic speaker";
constexpr uint32_t HELLO_PERIOD_MS  = 2000;

// Audio on the wire: mono, unsigned 8-bit (128 = silence), 16 kHz.
// 8 bits is all the speaker's DAC uses. Big packets on purpose: every packet
// the mic board sends puts a burst of noise into its microphone.
constexpr uint32_t SAMPLE_RATE      = 16000;
constexpr size_t SAMPLES_PER_PACKET = 1024;           // 64 ms per packet, 1 KB

// How the audio travels. false: WiFi UDP as above - clear sound, needs the BBC
// network. true: the on-board LoRa radio - no WiFi, ~1 km, walkie-talkie sound.
// LoRa carries ~5 kbit/s, not the 128 kbit/s above, so the voice is squeezed
// with the Codec2 speech codec: 8 kHz, 20 ms of speech -> 8 bytes.
constexpr bool USE_LORA = true;

// LoRa: both boards must match. 869.525 MHz is the part of the 868 MHz band
// that allows 10% transmit time (most of it allows 1%); voice uses ~75% while
// someone talks, so fine for testing, not for leaving it on all day.
constexpr long LORA_FREQUENCY_HZ    = 869525000;
constexpr int LORA_SPREADING_FACTOR = 7;       // the fastest; voice doesn't fit in slower ones
constexpr long LORA_BANDWIDTH_HZ    = 125000;
constexpr int LORA_SYNC_WORD        = 0x44;    // other LoRa projects (0x12 default) are ignored
constexpr int LORA_TX_POWER_DBM     = 14;      // the 868 MHz legal limit

constexpr uint32_t CODEC2_SAMPLE_RATE      = 8000;
constexpr size_t CODEC2_SAMPLES_PER_FRAME  = 160;  // 20 ms (Codec2 3200 bit/s mode)
constexpr size_t CODEC2_BYTES_PER_FRAME    = 8;
constexpr size_t CODEC2_FRAMES_PER_PACKET  = 8;    // 160 ms of speech per 64-byte packet, ~120 ms on air
constexpr size_t LORA_PACKET_SIZE = CODEC2_FRAMES_PER_PACKET * CODEC2_BYTES_PER_FRAME;
