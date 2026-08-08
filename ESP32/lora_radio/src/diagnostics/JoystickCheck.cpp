// Diagnostic: OLED + joystick only, no radio.
//
// The point is to split one question into two. If the scanner shows nothing, it
// could be the display, the joystick, or the radio. This build removes the radio
// from the picture entirely, so whatever you see here is purely about the screen
// and the stick.
//
//   pio run -e joystick-check -t upload
//
// What you should see: two live numbers that move when you push the stick, a
// crosshair that follows it, and the last event name changing. Push the stick to
// each extreme and the numbers should approach 0 and 4095.

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <Wire.h>

#include "../joystick.h"
#include "pins.h"

namespace {

constexpr uint8_t ScreenWidth = 128;
constexpr uint8_t ScreenHeight = 64;
constexpr uint8_t ScreenAddress = 0x3C;
constexpr int AdcMax = 4095;

// Live crosshair box, so you can see the stick's travel rather than read numbers.
constexpr uint8_t BoxX = 84;
constexpr uint8_t BoxY = 26;
constexpr uint8_t BoxSize = 36;

Adafruit_SSD1306 display(ScreenWidth, ScreenHeight, &Wire, OLED_RST);

bool displayReady = false;
const char* lastEvent = "-";

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\nJoystick + OLED check (no radio)");

    Wire.begin(OLED_SDA, OLED_SCL);
    Wire.setClock(400000);

    displayReady = display.begin(SSD1306_SWITCHCAPVCC, ScreenAddress);
    if (!displayReady) {
        // If this prints, the fault is the display or I2C, not the joystick.
        Serial.println("OLED init failed - check I2C / board version");
    }

    joystick::begin();
}

void loop() {
    const JoystickEvent event = joystick::poll();
    if (event != JoystickEvent::None) {
        lastEvent = joystick::eventName(event);
        Serial.printf("%s  X=%d Y=%d\n", lastEvent, joystick::rawX(), joystick::rawY());
    }

    if (!displayReady) {
        delay(50);
        return;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.print("JOYSTICK CHECK");
    display.drawFastHLine(0, 10, ScreenWidth, SSD1306_WHITE);

    display.setCursor(0, 16);
    display.printf("X %4d", joystick::rawX());
    display.setCursor(0, 26);
    display.printf("Y %4d", joystick::rawY());
    display.setCursor(0, 36);
    display.printf("C %4d/%4d", joystick::centreX(), joystick::centreY());

    display.setCursor(0, 48);
    display.print(joystick::detected() ? "stick ok" : "STICK? 3V3+GND");
    display.setCursor(0, 56);
    display.printf("%s %s", joystick::buttonDown() ? "BTN" : "   ", lastEvent);

    // Crosshair: maps raw ADC straight to the box, so a miswired axis is obvious.
    display.drawRect(BoxX, BoxY, BoxSize, BoxSize, SSD1306_WHITE);
    const uint8_t dotX = BoxX + 1 + map(constrain(joystick::rawX(), 0, AdcMax), 0, AdcMax, 0, BoxSize - 3);
    const uint8_t dotY = BoxY + 1 + map(constrain(joystick::rawY(), 0, AdcMax), 0, AdcMax, 0, BoxSize - 3);
    display.fillRect(dotX, dotY, 2, 2, SSD1306_WHITE);

    display.display();
    delay(30);
}
