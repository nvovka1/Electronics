#include "ui.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "battery.h"
#include "joystick.h"
#include "pins.h"
#include "radio.h"

namespace {

constexpr uint8_t ScreenWidth  = 128;
constexpr uint8_t ScreenHeight = 64;
constexpr uint8_t ScreenAddress = 0x3C;

// The default GFX font is 6x8 px, so a row is 8 px and a line holds 21 chars.
constexpr uint8_t LineHeight = 8;
constexpr uint8_t StatusBarHeight = 9;

// Graph area, shared by the SIGNAL and SWEEP pages.
constexpr uint8_t GraphTop    = 24;
constexpr uint8_t GraphBottom = ScreenHeight - 1;
constexpr int RssiFloorDbm = -130;
constexpr int RssiCeilDbm  = -20;

// 10 Hz. Fast enough to feel live, slow enough that the ~25 ms I2C frame write
// leaves the receive path plenty of room.
constexpr uint32_t RenderIntervalMs = 100;

constexpr uint8_t SettingsVisibleRows = 6;

Adafruit_SSD1306 display(ScreenWidth, ScreenHeight, &Wire, OLED_RST);

bool displayReady = false;
ui::Page currentPage = ui::Page::Live;
ui::Dialog currentDialog = ui::Dialog::None;
ui::SettingsRow currentRow = ui::SettingsRow::Preset;
uint8_t settingsScrollTop = 0;
bool hexEmphasis = false;
uint32_t lastRenderAt = 0;

void printAt(uint8_t x, uint8_t y, const char* text) {
    display.setCursor(x, y);
    display.print(text);
}

const char* pageName(ui::Page page) {
    switch (page) {
        case ui::Page::Live:     return "LIVE";
        case ui::Page::Packet:   return "PACKET";
        case ui::Page::Signal:   return "SIGNAL";
        case ui::Page::Sweep:    return "SWEEP";
        case ui::Page::Settings: return "SETTINGS";
        default:                 return "?";
    }
}

void formatMhz(char* out, size_t size, long hz) {
    snprintf(out, size, "%.3f", hz / 1000000.0);
}

void formatBandwidth(char* out, size_t size, long hz) {
    if (hz >= 1000L) {
        snprintf(out, size, "%.1fk", hz / 1000.0);
    } else {
        snprintf(out, size, "%ld", hz);
    }
}

uint8_t rssiToY(int rssi) {
    const int clamped = constrain(rssi, RssiFloorDbm, RssiCeilDbm);
    const long y = map(clamped, RssiFloorDbm, RssiCeilDbm, GraphBottom, GraphTop);
    return static_cast<uint8_t>(constrain(y, GraphTop, GraphBottom));
}

void drawStatusBar() {
    char buffer[24];

    display.setTextColor(SSD1306_WHITE);
    printAt(0, 0, pageName(currentPage));

    // Right-aligned battery, or a charge hint when it looks like USB power.
    if (battery::onUsbPower()) {
        snprintf(buffer, sizeof(buffer), "USB");
    } else {
        snprintf(buffer, sizeof(buffer), "%u%%", battery::percent());
    }
    const uint8_t width = strlen(buffer) * 6;
    printAt(ScreenWidth - width, 0, buffer);

    // A radio fault is the one thing that must be visible from every page.
    if (!radio::healthy()) {
        printAt(ScreenWidth - width - 20, 0, "RF!");
    } else if (radio::sweeping()) {
        printAt(ScreenWidth - width - 26, 0, "SWP");
    }

    display.drawFastHLine(0, StatusBarHeight - 2, ScreenWidth, SSD1306_WHITE);
}

// Filled area chart of RSSI. Used for both time history and the band sweep,
// because they are the same shape of data.
void drawGraph(const int8_t* samples, uint8_t count) {
    for (uint8_t i = 0; i < count && i < ScreenWidth; ++i) {
        if (samples[i] == radio::HistoryEmpty) continue;
        const uint8_t y = rssiToY(samples[i]);
        display.drawFastVLine(i, y, GraphBottom - y + 1, SSD1306_WHITE);
    }
}

void drawLivePage() {
    char buffer[32];
    char frequency[16];

    formatMhz(frequency, sizeof(frequency), radio::config().frequencyHz);
    display.setTextSize(2);
    printAt(0, 12, frequency);
    display.setTextSize(1);
    printAt(ScreenWidth - 18, 19, "MHz");

    snprintf(buffer, sizeof(buffer), "now %4d dBm", radio::currentRssi());
    printAt(0, 32, buffer);

    const radio::Packet& packet = radio::lastPacket();
    if (packet.valid) {
        snprintf(buffer, sizeof(buffer), "pkt %4d  %+.1fdB", packet.rssi, packet.snr);
    } else {
        snprintf(buffer, sizeof(buffer), "pkt   --  no decode");
    }
    printAt(0, 42, buffer);

    snprintf(buffer, sizeof(buffer), "n=%lu  %.1f/s  SF%u",
             static_cast<unsigned long>(radio::packetCount()), radio::packetsPerSecond(),
             radio::config().spreadingFactor);
    printAt(0, 54, buffer);
}

void drawPacketPage() {
    char buffer[32];
    const radio::Packet& packet = radio::lastPacket();

    if (!packet.valid) {
        printAt(0, 20, "No packet decoded");
        printAt(0, 32, "yet. This is normal");
        printAt(0, 42, "on a quiet band -");
        printAt(0, 52, "see README.");
        return;
    }

    const uint32_t ageMs = millis() - packet.receivedAt;
    snprintf(buffer, sizeof(buffer), "%uB %4ddBm %.1fs", packet.trueLength, packet.rssi,
             ageMs / 1000.0);
    printAt(0, 12, buffer);

    snprintf(buffer, sizeof(buffer), "SNR %+.1fdB fe%+ld", packet.snr, packet.frequencyError);
    printAt(0, 22, buffer);

    // ASCII first when readable, hex first when the user asked for hex. Most real
    // traffic is encrypted, so hex is usually the more useful view.
    const uint8_t asciiY = hexEmphasis ? 54 : 34;
    const uint8_t hexY   = hexEmphasis ? 34 : 44;

    char ascii[22] = {0};
    const uint8_t asciiLength = min<uint8_t>(packet.storedLength, 21);
    for (uint8_t i = 0; i < asciiLength; ++i) {
        const uint8_t c = packet.data[i];
        ascii[i] = (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
    }
    printAt(0, asciiY, ascii);

    // Two rows of hex, 7 bytes each at 3 chars per byte.
    for (uint8_t row = 0; row < 2; ++row) {
        char hex[24] = {0};
        uint8_t offset = 0;
        for (uint8_t i = 0; i < 7; ++i) {
            const uint8_t index = row * 7 + i;
            if (index >= packet.storedLength) break;
            offset += snprintf(hex + offset, sizeof(hex) - offset, "%02X ", packet.data[index]);
        }
        printAt(0, hexY + row * LineHeight, hex);
    }
}

void drawSignalPage() {
    char buffer[32];
    char frequency[16];

    formatMhz(frequency, sizeof(frequency), radio::config().frequencyHz);
    snprintf(buffer, sizeof(buffer), "%s  %4d dBm", frequency, radio::currentRssi());
    printAt(0, 12, buffer);

    display.drawFastHLine(0, GraphTop - 2, ScreenWidth, SSD1306_WHITE);
    drawGraph(radio::rssiHistory(), radio::HistorySize);
}

void drawSweepPage() {
    char buffer[32];
    char frequency[16];

    const uint8_t cursor = radio::sweepCursor();
    formatMhz(frequency, sizeof(frequency), radio::sweepFrequencyAt(cursor));

    if (radio::sweeping()) {
        // Saying this plainly beats letting the user wonder why decoding stopped.
        snprintf(buffer, sizeof(buffer), "sweeping - RX off");
    } else if (radio::sweepHasData()) {
        snprintf(buffer, sizeof(buffer), "click=sweep again");
    } else {
        snprintf(buffer, sizeof(buffer), "click to sweep");
    }
    printAt(0, 12, buffer);

    // Before the first pass there is no measurement to show, and printing the
    // empty buffer's 0 would read as a wildly strong signal.
    if (radio::sweepHasData()) {
        snprintf(buffer, sizeof(buffer), "%s %4d dBm", frequency, radio::sweepFloor()[cursor]);
    } else {
        snprintf(buffer, sizeof(buffer), "%s   -- dBm", frequency);
    }
    printAt(0, 22, buffer);

    display.drawFastHLine(0, GraphTop + 8, ScreenWidth, SSD1306_WHITE);

    // Same shape as the signal graph but shifted down to make room for two header
    // lines, so it is drawn here rather than through drawGraph().
    const int8_t* floorSamples = radio::sweepFloor();
    for (uint8_t i = 0; i < radio::SweepBins && i < ScreenWidth; ++i) {
        if (floorSamples[i] == radio::HistoryEmpty) continue;
        const int clamped = constrain(floorSamples[i], RssiFloorDbm, RssiCeilDbm);
        const long y = map(clamped, RssiFloorDbm, RssiCeilDbm, GraphBottom, GraphTop + 10);
        const uint8_t top = static_cast<uint8_t>(constrain(y, GraphTop + 10, GraphBottom));
        display.drawFastVLine(i, top, GraphBottom - top + 1, SSD1306_WHITE);
    }

    // Cursor, drawn inverted so it reads over the filled bars.
    for (uint8_t y = GraphTop + 10; y <= GraphBottom; y += 2) {
        display.drawPixel(cursor, y, SSD1306_INVERSE);
    }
}

void settingsRowText(ui::SettingsRow row, char* out, size_t size) {
    const radio::Config& config = radio::config();
    char value[20];

    switch (row) {
        case ui::SettingsRow::Preset:
            snprintf(out, size, "Preset %s", radio::presetName(radio::currentPreset()));
            return;
        case ui::SettingsRow::SpreadingFactor:
            snprintf(out, size, "SF     %u", config.spreadingFactor);
            return;
        case ui::SettingsRow::Bandwidth:
            formatBandwidth(value, sizeof(value), config.bandwidthHz);
            snprintf(out, size, "BW     %s", value);
            return;
        case ui::SettingsRow::CodingRate:
            snprintf(out, size, "CR     4/%u", config.codingRate);
            return;
        case ui::SettingsRow::SyncWord:
            snprintf(out, size, "Sync   0x%02X", config.syncWord);
            return;
        case ui::SettingsRow::StepSize:
            formatBandwidth(value, sizeof(value), radio::stepSizeHz());
            snprintf(out, size, "Step   %sHz", value);
            return;
        case ui::SettingsRow::Stick:
            snprintf(out, size, "Stick %4d %4d", joystick::rawX(), joystick::rawY());
            return;
        default:
            snprintf(out, size, "?");
            return;
    }
}

void drawSettingsPage() {
    const uint8_t rowCount = static_cast<uint8_t>(ui::SettingsRow::Count);
    const uint8_t selected = static_cast<uint8_t>(currentRow);

    // Keep the selection inside the visible window.
    if (selected < settingsScrollTop) settingsScrollTop = selected;
    if (selected >= settingsScrollTop + SettingsVisibleRows) {
        settingsScrollTop = selected - SettingsVisibleRows + 1;
    }

    for (uint8_t i = 0; i < SettingsVisibleRows; ++i) {
        const uint8_t index = settingsScrollTop + i;
        if (index >= rowCount) break;

        char text[24];
        settingsRowText(static_cast<ui::SettingsRow>(index), text, sizeof(text));

        const uint8_t y = 11 + i * LineHeight;
        if (index == selected) {
            display.fillRect(0, y - 1, ScreenWidth, LineHeight, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
            printAt(1, y, text);
            display.setTextColor(SSD1306_WHITE);
        } else {
            printAt(1, y, text);
        }
    }
}

void drawDialog() {
    display.fillRect(4, 12, ScreenWidth - 8, ScreenHeight - 18, SSD1306_BLACK);
    display.drawRect(4, 12, ScreenWidth - 8, ScreenHeight - 18, SSD1306_WHITE);

    switch (currentDialog) {
        case ui::Dialog::SelfTestConfirm:
            printAt(8, 16, "TRANSMIT TEST");
            printAt(8, 28, "Antenna attached?");
            printAt(8, 38, "Sends 1 packet.");
            printAt(8, 52, "click=yes  Y=cancel");
            break;
        case ui::Dialog::SelfTestPassed:
            printAt(8, 16, "TX PATH OK");
            printAt(8, 28, "Radio transmitted.");
            printAt(8, 38, "Says nothing about");
            printAt(8, 48, "reception.");
            break;
        case ui::Dialog::SelfTestFailed:
            printAt(8, 16, "TX FAILED");
            printAt(8, 28, "Radio did not send.");
            printAt(8, 38, "Check power/wiring.");
            break;
        default:
            break;
    }
}

}  // namespace

namespace ui {

bool begin() {
    Wire.begin(OLED_SDA, OLED_SCL);

    // Without a timeout the ESP32 I2C driver can spin forever on an unresponsive
    // bus, with interrupts masked — which shows up as a silent watchdog reset
    // rather than a readable error. This turns that into a failed return instead.
    Wire.setTimeOut(50);

    // reset=true  — the SSD1306 on this board needs its hardware reset pulse.
    // periphBegin=false — the bus is already up; letting Adafruit call Wire.begin()
    // a second time re-initialises a live I2C peripheral and is what wedged it.
    displayReady = display.begin(SSD1306_SWITCHCAPVCC, ScreenAddress, true, false);
    if (!displayReady) {
        Serial.println("OLED init FAILED - check I2C wiring");
        return false;
    }
    Serial.println("OLED init OK");

    // Only once the panel is initialised: 400 kHz keeps a full-frame write near
    // 25 ms instead of 90 ms, which is what keeps the receive path responsive.
    Wire.setClock(400000);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.display();
    return true;
}

void showSplash(bool radioOk, bool joystickOk) {
    if (!displayReady) return;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    printAt(0, 0, "LoRa band scanner");
    display.drawFastHLine(0, 10, ScreenWidth, SSD1306_WHITE);

    printAt(0, 16, radioOk ? "SX1276  ok" : "SX1276  FAIL - SPI?");
    printAt(0, 26, joystickOk ? "Stick   ok" : "Stick   ? check 3V3");

    char buffer[24];
    formatMhz(buffer, sizeof(buffer), radio::config().frequencyHz);
    display.setCursor(0, 38);
    display.printf("%s MHz SF%u", buffer, radio::config().spreadingFactor);

    printAt(0, 52, "Y=page X=tune");
    display.display();
}

void changePage(int delta) {
    const int count = static_cast<int>(Page::Count);
    int next = (static_cast<int>(currentPage) + delta) % count;
    if (next < 0) next += count;
    currentPage = static_cast<Page>(next);
}

Page page() { return currentPage; }

void moveSettingsRow(int delta) {
    const int count = static_cast<int>(SettingsRow::Count);
    int next = (static_cast<int>(currentRow) + delta) % count;
    if (next < 0) next += count;
    currentRow = static_cast<SettingsRow>(next);
}

SettingsRow settingsRow() { return currentRow; }

void toggleHexEmphasis() { hexEmphasis = !hexEmphasis; }

void showDialog(Dialog next) { currentDialog = next; }

Dialog dialog() { return currentDialog; }

void render() {
    if (!displayReady) return;

    const uint32_t now = millis();
    if (now - lastRenderAt < RenderIntervalMs) return;
    lastRenderAt = now;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    drawStatusBar();

    switch (currentPage) {
        case Page::Live:     drawLivePage(); break;
        case Page::Packet:   drawPacketPage(); break;
        case Page::Signal:   drawSignalPage(); break;
        case Page::Sweep:    drawSweepPage(); break;
        case Page::Settings: drawSettingsPage(); break;
        default: break;
    }

    if (currentDialog != Dialog::None) drawDialog();

    display.display();
}

}  // namespace ui
