#include "radio.h"

#include <LoRa.h>
#include <SPI.h>

#include "pins.h"

namespace {

// Bandwidths the SX1276 actually supports, in the order the library accepts them.
constexpr long Bandwidths[] = {
    7800L, 10400L, 15600L, 20800L, 31250L, 41700L, 62500L, 125000L, 250000L, 500000L,
};
constexpr uint8_t BandwidthCount = sizeof(Bandwidths) / sizeof(Bandwidths[0]);

// Tuning increments, from fine to coarse.
constexpr long StepSizes[] = {25000L, 100000L, 200000L, 1000000L};
constexpr uint8_t StepSizeCount = sizeof(StepSizes) / sizeof(StepSizes[0]);

constexpr uint32_t RssiSampleIntervalMs = 50;

// Bins are stepped a few at a time so a sweep never blocks the UI for its full
// duration and can be abandoned mid-pass.
constexpr uint8_t SweepBinsPerPoll = 4;
constexpr uint32_t SweepSettleMs = 5;

// EU 868.0-868.6 MHz permits 25 mW (14 dBm) at a 1 % duty cycle, so a single
// short packet is comfortably inside the rules.
constexpr uint8_t SelfTestTxPower = 14;

constexpr uint32_t RateWindowMs = 5000;

radio::Config activeConfig;
radio::Preset activePreset = radio::Preset::LoRaWanSf7;
radio::Packet latestPacket;

bool radioHealthy = false;

uint8_t stepSizeIndex = 1;  // 100 kHz
int8_t rssiHistoryBuffer[radio::HistorySize] = {0};
uint32_t lastRssiSampleAt = 0;
int instantRssi = 0;

uint32_t totalPackets = 0;
// Ring of recent arrival times, used to derive a rate without a timer.
uint32_t recentArrivals[32] = {0};
uint8_t recentArrivalHead = 0;

// Set from the DIO0 interrupt when a packet lands. The handler does the absolute
// minimum — no SPI, no floating point, no Serial — because all three are unsafe
// in ISR context on the ESP32. The FIFO pointer stays put until the next packet,
// so the payload and its metrics are read back safely from poll().
volatile bool packetPending = false;
volatile int pendingSize = 0;

void IRAM_ATTR onLoRaReceive(int packetSize) {
    pendingSize = packetSize;
    packetPending = true;
}

bool sweepRunning = false;
bool sweepFilled = false;
uint8_t sweepBin = 0;
uint8_t sweepCursorBin = radio::SweepBins / 2;
int8_t sweepFloorBuffer[radio::SweepBins] = {0};
long frequencyBeforeSweep = 0;

// Built field by field rather than with a brace initialiser: Config carries
// default member initialisers, which stops it being an aggregate under the
// gnu++11 that the ESP32 Arduino core compiles with.
radio::Config makeConfig(long frequencyHz, uint8_t spreadingFactor, long bandwidthHz,
                         uint8_t codingRate, uint8_t syncWord) {
    radio::Config config;
    config.frequencyHz = frequencyHz;
    config.spreadingFactor = spreadingFactor;
    config.bandwidthHz = bandwidthHz;
    config.codingRate = codingRate;
    config.syncWord = syncWord;
    return config;
}

uint8_t bandwidthIndex() {
    for (uint8_t i = 0; i < BandwidthCount; ++i) {
        if (Bandwidths[i] == activeConfig.bandwidthHz) return i;
    }
    return 7;  // 125 kHz
}

// Pushes the whole config to the chip and returns to listening. Going through
// idle() first avoids reading RSSI or IRQ flags while the modem is mid-transition.
void applyConfig() {
    LoRa.idle();
    LoRa.setFrequency(activeConfig.frequencyHz);
    LoRa.setSpreadingFactor(activeConfig.spreadingFactor);
    LoRa.setSignalBandwidth(activeConfig.bandwidthHz);
    LoRa.setCodingRate4(activeConfig.codingRate);
    LoRa.setSyncWord(activeConfig.syncWord);
    LoRa.enableCrc();
    LoRa.receive();
}

void recordArrival(uint32_t now) {
    recentArrivals[recentArrivalHead] = now;
    recentArrivalHead = (recentArrivalHead + 1) % 32;
}

void capturePacket(int packetSize, uint32_t now) {
    latestPacket.trueLength = static_cast<uint16_t>(packetSize);
    latestPacket.storedLength = 0;
    while (LoRa.available() && latestPacket.storedLength < radio::MaxStoredPayload) {
        latestPacket.data[latestPacket.storedLength++] = static_cast<uint8_t>(LoRa.read());
    }
    // Drain anything past the stored window so the FIFO is clean for the next one.
    while (LoRa.available()) LoRa.read();

    latestPacket.rssi = LoRa.packetRssi();
    latestPacket.snr = LoRa.packetSnr();
    latestPacket.frequencyError = LoRa.packetFrequencyError();
    latestPacket.receivedAt = now;
    latestPacket.valid = true;

    ++totalPackets;
    recordArrival(now);

    Serial.printf("[%lu] %d bytes  RSSI %d dBm  SNR %.1f dB  ferr %ld Hz\n",
                  static_cast<unsigned long>(now), packetSize, latestPacket.rssi,
                  latestPacket.snr, latestPacket.frequencyError);
}

void sampleRssi(uint32_t now) {
    if (now - lastRssiSampleAt < RssiSampleIntervalMs) return;
    lastRssiSampleAt = now;

    instantRssi = LoRa.rssi();

    for (uint8_t i = 0; i < radio::HistorySize - 1; ++i) {
        rssiHistoryBuffer[i] = rssiHistoryBuffer[i + 1];
    }
    rssiHistoryBuffer[radio::HistorySize - 1] = static_cast<int8_t>(constrain(instantRssi, -128, 0));
}

void advanceSweep() {
    for (uint8_t i = 0; i < SweepBinsPerPoll && sweepRunning; ++i) {
        const long frequency = radio::sweepFrequencyAt(sweepBin);

        LoRa.idle();
        LoRa.setFrequency(frequency);
        LoRa.receive();
        delay(SweepSettleMs);
        sweepFloorBuffer[sweepBin] = static_cast<int8_t>(constrain(LoRa.rssi(), -128, 0));

        ++sweepBin;
        if (sweepBin >= radio::SweepBins) {
            sweepBin = 0;
            sweepFilled = true;
        }
    }
}

}  // namespace

namespace radio {

bool begin() {
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

    // begin() checks the chip's version register, so a false here means the SPI
    // wiring is wrong or the radio is dead.
    if (!LoRa.begin(activeConfig.frequencyHz)) {
        radioHealthy = false;
        Serial.println("LoRa init FAILED - SX1276 did not answer");
        return false;
    }

    radioHealthy = true;

    // Interrupt-driven reception, not polling. LoRa.parsePacket() forces the chip
    // into single-RX mode every call, which fights the continuous RX this needs —
    // continuous mode is what keeps rssi() meaningful for the graph and sweep.
    LoRa.onReceive(onLoRaReceive);
    applyConfig();  // ends in LoRa.receive(), arming continuous RX

    Serial.println("LoRa init OK");
    return true;
}

bool healthy() { return radioHealthy; }

void poll() {
    if (!radioHealthy) return;

    const uint32_t now = millis();

    if (sweepRunning) {
        packetPending = false;  // retuning constantly; anything flagged is noise
        advanceSweep();
        return;                 // cannot decode while hopping
    }

    if (packetPending) {
        packetPending = false;
        capturePacket(pendingSize, now);
    }

    sampleRssi(now);
}

void tune(int steps) {
    if (steps == 0) return;
    long frequency = activeConfig.frequencyHz + static_cast<long>(steps) * stepSizeHz();
    frequency = constrain(frequency, BandLowHz, BandHighHz);
    if (frequency == activeConfig.frequencyHz) return;

    activeConfig.frequencyHz = frequency;
    applyConfig();
}

void nextStepSize(int delta) {
    const int next = static_cast<int>(stepSizeIndex) + delta;
    stepSizeIndex = static_cast<uint8_t>(constrain(next, 0, StepSizeCount - 1));
}

long stepSizeHz() { return StepSizes[stepSizeIndex]; }

void adjustSpreadingFactor(int delta) {
    // SF6 is excluded on purpose: it only works in implicit-header mode, which
    // would silently stop this receiver from decoding anything normal.
    const int next = constrain(static_cast<int>(activeConfig.spreadingFactor) + delta, 7, 12);
    if (next == activeConfig.spreadingFactor) return;
    activeConfig.spreadingFactor = static_cast<uint8_t>(next);
    applyConfig();
}

void adjustBandwidth(int delta) {
    const int next = constrain(static_cast<int>(bandwidthIndex()) + delta, 0, BandwidthCount - 1);
    if (Bandwidths[next] == activeConfig.bandwidthHz) return;
    activeConfig.bandwidthHz = Bandwidths[next];
    applyConfig();
}

void adjustCodingRate(int delta) {
    const int next = constrain(static_cast<int>(activeConfig.codingRate) + delta, 5, 8);
    if (next == activeConfig.codingRate) return;
    activeConfig.codingRate = static_cast<uint8_t>(next);
    applyConfig();
}

void adjustSyncWord(int delta) {
    activeConfig.syncWord = static_cast<uint8_t>(activeConfig.syncWord + delta);
    applyConfig();
}

void applyPreset(Preset preset) {
    switch (preset) {
        case Preset::LoRaWanSf7:
            activeConfig = makeConfig(868100000L, 7, 125000L, 5, 0x34);
            break;
        case Preset::LoRaWanSf9:
            activeConfig = makeConfig(868100000L, 9, 125000L, 5, 0x34);
            break;
        case Preset::LoRaWanSf12:
            activeConfig = makeConfig(868100000L, 12, 125000L, 5, 0x34);
            break;
        case Preset::MeshtasticLongFast:
            activeConfig = makeConfig(869525000L, 11, 250000L, 5, 0x2B);
            break;
        case Preset::Private:
            activeConfig = makeConfig(868000000L, 7, 125000L, 5, 0x12);
            break;
        default:
            return;
    }
    activePreset = preset;
    if (radioHealthy) applyConfig();
}

void cyclePreset(int delta) {
    const int count = static_cast<int>(Preset::Count);
    int next = (static_cast<int>(activePreset) + delta) % count;
    if (next < 0) next += count;
    applyPreset(static_cast<Preset>(next));
}

const Config& config() { return activeConfig; }
Preset currentPreset() { return activePreset; }

const char* presetName(Preset preset) {
    switch (preset) {
        case Preset::LoRaWanSf7:         return "LoRaWAN SF7";
        case Preset::LoRaWanSf9:         return "LoRaWAN SF9";
        case Preset::LoRaWanSf12:        return "LoRaWAN SF12";
        case Preset::MeshtasticLongFast: return "Mesh LongFast";
        case Preset::Private:            return "Private 0x12";
        default:                         return "?";
    }
}

int currentRssi() { return instantRssi; }
const Packet& lastPacket() { return latestPacket; }
uint32_t packetCount() { return totalPackets; }

float packetsPerSecond() {
    const uint32_t now = millis();
    uint8_t count = 0;
    for (uint8_t i = 0; i < 32; ++i) {
        if (recentArrivals[i] != 0 && now - recentArrivals[i] <= RateWindowMs) ++count;
    }
    return count / (RateWindowMs / 1000.0f);
}

void resetStatistics() {
    totalPackets = 0;
    latestPacket.valid = false;
    for (uint8_t i = 0; i < 32; ++i) recentArrivals[i] = 0;
    recentArrivalHead = 0;
}

const int8_t* rssiHistory() { return rssiHistoryBuffer; }

void clearRssiHistory() {
    for (uint8_t i = 0; i < HistorySize; ++i) rssiHistoryBuffer[i] = HistoryEmpty;
}

void startSweep() {
    if (!radioHealthy || sweepRunning) return;
    frequencyBeforeSweep = activeConfig.frequencyHz;
    sweepRunning = true;
    sweepBin = 0;
}

void stopSweep() {
    if (!sweepRunning) return;
    sweepRunning = false;
    activeConfig.frequencyHz = frequencyBeforeSweep;
    applyConfig();
}

bool sweeping() { return sweepRunning; }
bool sweepHasData() { return sweepFilled || sweepBin > 0; }
const int8_t* sweepFloor() { return sweepFloorBuffer; }

long sweepFrequencyAt(uint8_t bin) {
    const long span = BandHighHz - BandLowHz;
    return BandLowHz + (span / SweepBins) * static_cast<long>(bin);
}

void moveSweepCursor(int delta) {
    const int next = constrain(static_cast<int>(sweepCursorBin) + delta, 0, SweepBins - 1);
    sweepCursorBin = static_cast<uint8_t>(next);
}

uint8_t sweepCursor() { return sweepCursorBin; }

bool selfTest() {
    if (!radioHealthy) return false;

    const bool wasSweeping = sweepRunning;
    if (wasSweeping) stopSweep();

    LoRa.idle();
    LoRa.setTxPower(SelfTestTxPower);

    const bool started = LoRa.beginPacket() == 1;
    if (started) {
        LoRa.print("lora_radio selftest");
    }
    const bool sent = started && LoRa.endPacket() == 1;

    applyConfig();  // back to listening

    Serial.printf("Self-test transmit: %s\n", sent ? "OK" : "FAILED");
    return sent;
}

}  // namespace radio
