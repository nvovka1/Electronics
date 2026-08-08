// SX1276 as a receive-only band monitor.
//
// Owns all radio state: modulation settings, the last packet, running statistics,
// an RSSI history for the graph, and the band sweep. Knows nothing about the
// display.
//
// Worth remembering what this chip can actually do: it demodulates only packets
// whose spreading factor, bandwidth, coding rate and sync word match its own
// settings exactly. There is no promiscuous mode and no wideband demodulation, so
// "scanning" here means two separate things — sweeping RSSI to find energy on the
// band (always works), and decoding packets (only when the settings happen to
// match a real transmitter).

#pragma once

#include <Arduino.h>

namespace radio {

// Payloads are truncated to this for storage; the true length is kept separately.
constexpr uint8_t MaxStoredPayload = 64;

// Width of both the RSSI history graph and the sweep, chosen to map one sample to
// one pixel column on a 128 px display.
constexpr uint8_t HistorySize = 128;
constexpr uint8_t SweepBins   = 128;

constexpr long BandLowHz  = 863000000L;
constexpr long BandHighHz = 870000000L;

struct Packet {
    uint8_t data[MaxStoredPayload] = {0};
    uint8_t storedLength = 0;   // bytes actually kept
    uint16_t trueLength = 0;    // bytes the radio reported
    int rssi = 0;
    float snr = 0.0f;
    long frequencyError = 0;
    uint32_t receivedAt = 0;
    bool valid = false;
};

struct Config {
    long frequencyHz = 868100000L;  // LoRaWAN EU868 uplink channel 1
    uint8_t spreadingFactor = 7;
    long bandwidthHz = 125000L;
    uint8_t codingRate = 5;         // denominator: 4/5
    uint8_t syncWord = 0x34;        // LoRaWAN
};

enum class Preset : uint8_t {
    LoRaWanSf7,
    LoRaWanSf9,
    LoRaWanSf12,
    MeshtasticLongFast,
    Private,
    Count,
};

// Returns false when the SX1276 does not answer with the expected version
// register, which means SPI wiring or a dead radio — not merely a quiet band.
bool begin();
bool healthy();

// Call every loop(). Handles packet reception and drives the sweep.
void poll();

// --- Tuning and settings. Each applies immediately and re-enters receive mode.

void tune(int steps);          // steps of the current tuning step size
void nextStepSize(int delta);
long stepSizeHz();

void adjustSpreadingFactor(int delta);  // clamped to 7-12
void adjustBandwidth(int delta);        // walks the library's discrete list
void adjustCodingRate(int delta);       // 5-8
void adjustSyncWord(int delta);
void applyPreset(Preset preset);
void cyclePreset(int delta);

const Config& config();
Preset currentPreset();
const char* presetName(Preset preset);

// --- Statistics

int currentRssi();               // instantaneous, from the radio's RSSI register
const Packet& lastPacket();
uint32_t packetCount();
float packetsPerSecond();
void resetStatistics();

// RSSI over time, newest last. Values are dBm; HistoryEmpty marks unfilled slots.
constexpr int HistoryEmpty = 0;
const int8_t* rssiHistory();
void clearRssiHistory();

// --- Band sweep. Mutually exclusive with receiving, because the radio has to
// retune for every bin — nothing can be decoded while a sweep runs.

void startSweep();
void stopSweep();
bool sweeping();
bool sweepHasData();
const int8_t* sweepFloor();     // SweepBins entries, dBm
long sweepFrequencyAt(uint8_t bin);
void moveSweepCursor(int delta);
uint8_t sweepCursor();

// --- Transmit self-test
//
// One short packet at a low, EU-legal power. Proves the transmit path and that
// the board does not brown out. Proves nothing about reception, and requires an
// antenna to be attached first.
bool selfTest();

}  // namespace radio
