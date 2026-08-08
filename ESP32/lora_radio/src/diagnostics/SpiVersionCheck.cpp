// Raw SPI register read against the on-board SX1276 — no LoRa library involved.
//
//   pio run -e spi-check -t upload -t monitor
//
// Reads the chip's version register (0x42), which must answer 0x12 on an
// SX1276/77/78/79. Because nothing but SPI is running, a correct answer here
// proves the bus, the pin mapping, the chip select and the reset line all work.
//
// The transaction, per the datasheet:
//
//   1. NSS/CS -> LOW                      (opens the transaction)
//   2. send the address byte              bit7 = 0 for read, bits6..0 = address
//   3. send a second, dummy byte (0x00)   only to generate 8 more clock edges
//   4. read 8 bits off MISO while that dummy byte is going out
//   5. NSS/CS -> HIGH                     (closes the transaction)
//
// SPI is full duplex: step 3 and step 4 are the same eight clock cycles. The
// dummy byte exists purely to make the clock run — the chip shifts the register
// contents out on MISO at the same time as the master shifts zeros out on MOSI.
//
// Two independent implementations run below and must agree:
//   readRegisterBitBanged() — plain digitalWrite/digitalRead, one bit at a time
//   readRegisterHardware()  — the ESP32 SPI peripheral
// If only the hardware one fails, the problem is bus configuration. If both
// fail, it is wiring or the reset line.

#include <Arduino.h>
#include <SPI.h>

#include "pins.h"

namespace {

// SX1276 register addresses (datasheet section 6.4, "Register Map").
constexpr uint8_t RegOpMode     = 0x01;
constexpr uint8_t RegFrfMsb     = 0x06;
constexpr uint8_t RegFrfMid     = 0x07;
constexpr uint8_t RegFrfLsb     = 0x08;
constexpr uint8_t RegVersion    = 0x42;

constexpr uint8_t ExpectedVersion = 0x12;

// bit7 of the address byte selects direction: 0 = read, 1 = write.
constexpr uint8_t ReadMask  = 0x7F;
constexpr uint8_t WriteMask = 0x80;

// The SX1276 accepts up to 10 MHz. 1 MHz is far below that and leaves plenty of
// margin if the pin headers ever get extended with jumper wires.
constexpr uint32_t SpiClockHz = 1000000;

// Half a clock period for the bit-banged version. 2 us gives a 250 kHz clock —
// glacial for the chip, but it makes the per-bit trace readable.
constexpr uint32_t BitDelayUs = 2;

constexpr uint32_t PollIntervalMs = 3000;

bool tracedOnce = false;

// --- Reset -----------------------------------------------------------------
//
// NRESET is active LOW. The datasheet asks for at least 100 us low, then 5 ms
// of settling before the chip will answer on SPI. The generous values here cost
// nothing at boot.
void resetRadio() {
    pinMode(LORA_RST, OUTPUT);
    digitalWrite(LORA_RST, LOW);
    delay(10);
    digitalWrite(LORA_RST, HIGH);
    delay(10);
}

// --- Implementation 1: bit-banged, pure GPIO -------------------------------
//
// SPI mode 0: the clock idles LOW, the master presents a bit on MOSI while the
// clock is LOW, and both sides sample on the RISING edge. So the order inside
// the loop is: drive MOSI, raise SCK, sample MISO, lower SCK.

void bitBangBegin() {
    pinMode(LORA_SCK, OUTPUT);
    digitalWrite(LORA_SCK, LOW);     // mode 0 idles the clock low
    pinMode(LORA_MOSI, OUTPUT);
    digitalWrite(LORA_MOSI, LOW);
    pinMode(LORA_MISO, INPUT);
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);     // CS idles HIGH — the chip ignores the bus
}

uint8_t bitBangTransfer(uint8_t out, bool trace) {
    uint8_t in = 0;

    for (int8_t bit = 7; bit >= 0; --bit) {   // MSB first
        const uint8_t outBit = (out >> bit) & 0x01;

        digitalWrite(LORA_MOSI, outBit);
        delayMicroseconds(BitDelayUs);

        digitalWrite(LORA_SCK, HIGH);         // rising edge: both sides sample
        const uint8_t inBit = digitalRead(LORA_MISO) ? 1 : 0;
        in = static_cast<uint8_t>((in << 1) | inBit);
        delayMicroseconds(BitDelayUs);

        digitalWrite(LORA_SCK, LOW);

        if (trace) {
            Serial.printf("      bit %d  MOSI=%u  MISO=%u\n", bit, outBit, inBit);
        }
    }

    return in;
}

uint8_t readRegisterBitBanged(uint8_t address, bool trace = false) {
    const uint8_t addressByte = address & ReadMask;   // bit7 = 0 -> read

    digitalWrite(LORA_CS, LOW);                      // step 1
    delayMicroseconds(BitDelayUs);

    if (trace) Serial.printf("    address byte 0x%02X:\n", addressByte);
    bitBangTransfer(addressByte, trace);             // step 2

    if (trace) Serial.println("    dummy byte 0x00 (reply arrives here):");
    const uint8_t value = bitBangTransfer(0x00, trace);  // steps 3 + 4

    delayMicroseconds(BitDelayUs);
    digitalWrite(LORA_CS, HIGH);                     // step 5

    return value;
}

// --- Implementation 2: the ESP32 SPI peripheral ----------------------------
//
// Same five steps, but the hardware shifts the bits. Note that CS is driven by
// hand rather than handed to the peripheral: the SX1276 needs CS to frame the
// whole two-byte exchange, and the Arduino SPI API has no portable way to
// express that.

void hardwareSpiBegin() {
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
}

uint8_t readRegisterHardware(uint8_t address) {
    const uint8_t addressByte = address & ReadMask;

    SPI.beginTransaction(SPISettings(SpiClockHz, MSBFIRST, SPI_MODE0));
    digitalWrite(LORA_CS, LOW);          // step 1

    SPI.transfer(addressByte);           // step 2 — reply to this byte is garbage
    const uint8_t value = SPI.transfer(0x00);  // steps 3 + 4 in one call

    digitalWrite(LORA_CS, HIGH);         // step 5
    SPI.endTransaction();

    return value;
}

// Writes exist for completeness: same framing, bit7 of the address set.
void writeRegisterHardware(uint8_t address, uint8_t value) {
    SPI.beginTransaction(SPISettings(SpiClockHz, MSBFIRST, SPI_MODE0));
    digitalWrite(LORA_CS, LOW);
    SPI.transfer(address | WriteMask);
    SPI.transfer(value);
    digitalWrite(LORA_CS, HIGH);
    SPI.endTransaction();
}

// --- Reporting -------------------------------------------------------------

void printBinary(uint8_t value) {
    for (int8_t bit = 7; bit >= 0; --bit) {
        Serial.print((value >> bit) & 0x01);
        if (bit == 4) Serial.print(' ');
    }
}

void reportVersion(uint8_t value, const char* how) {
    Serial.printf("  %-12s 0x%02X  (0b", how, value);
    printBinary(value);
    Serial.print(')');

    if (value == ExpectedVersion) {
        Serial.println("  <- correct, SX1276 responding");
    } else if (value == 0x00 || value == 0xFF) {
        Serial.println("  <- no reply (MISO stuck, check CS/reset/MISO)");
    } else {
        Serial.println("  <- unexpected value");
    }
}

// The carrier frequency lives in three registers as a 24-bit word, stepped by
// the crystal: Frf = frequency / (32 MHz / 2^19). Decoding it is a second,
// independent confirmation that reads are returning real data rather than noise
// that happens to look like 0x12.
void reportFrequency() {
    const uint32_t msb = readRegisterHardware(RegFrfMsb);
    const uint32_t mid = readRegisterHardware(RegFrfMid);
    const uint32_t lsb = readRegisterHardware(RegFrfLsb);

    const uint32_t frf = (msb << 16) | (mid << 8) | lsb;
    const double hz = static_cast<double>(frf) * 32000000.0 / 524288.0;  // 2^19

    Serial.printf("  Frf          0x%06lX -> %.3f MHz\n",
                  static_cast<unsigned long>(frf), hz / 1000000.0);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println("\n\nSX1276 raw SPI register read");
    Serial.printf("Pins: SCK=%d MISO=%d MOSI=%d CS=%d RST=%d\n\n",
                  LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS, LORA_RST);

    resetRadio();

    // Bit-banged first, while the SPI peripheral is not yet driving the pins.
    Serial.println("Bit-banged read of register 0x42, bit by bit:");
    bitBangBegin();
    const uint8_t banged = readRegisterBitBanged(RegVersion, true);
    tracedOnce = true;
    Serial.println();
    reportVersion(banged, "bit-banged");

    // Handing the same pins to the SPI peripheral. Releasing them first keeps
    // the two implementations from fighting over the pin matrix.
    pinMode(LORA_SCK, INPUT);
    pinMode(LORA_MOSI, INPUT);
    hardwareSpiBegin();

    const uint8_t hardware = readRegisterHardware(RegVersion);
    reportVersion(hardware, "hardware SPI");

    if (banged != hardware) {
        Serial.println("  !! the two methods disagree - marginal timing or wiring");
    }

    Serial.println();
}

void loop() {
    static uint32_t nextPollAt = 0;
    if (millis() < nextPollAt) return;
    nextPollAt = millis() + PollIntervalMs;

    Serial.printf("[%lus] register dump\n",
                  static_cast<unsigned long>(millis() / 1000));

    reportVersion(readRegisterHardware(RegVersion), "0x42 version");
    Serial.printf("  RegOpMode    0x%02X\n", readRegisterHardware(RegOpMode));
    reportFrequency();
    Serial.println();
}
