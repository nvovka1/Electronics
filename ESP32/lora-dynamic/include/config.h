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
