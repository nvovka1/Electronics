#pragma once

// Bar-graph audio spectrum for the 128x64 OLED, shared by both boards:
// 0..8 kHz left to right (62.5 Hz per column), bar height = loudness, with
// slowly falling peak markers. Copied from lora-mic's SPEC view.

#include <Adafruit_SSD1306.h>
#include <arduinoFFT.h>
#include "config.h"

class Spectrum {
 public:
  static constexpr int Top = 10;              // rows above are the status line
  static constexpr int Height = 64 - Top;     // 54 px bars
  static constexpr int Width = 128;           // one bar per FFT bin
  static constexpr uint16_t FftSize = 256;    // 128 bins of 62.5 Hz at 16 kHz
  // Bar range in dB of full scale: below FloorDb is empty, CeilingDb is full height.
  static constexpr float FloorDb = -70.0f;
  static constexpr float CeilingDb = -20.0f;

  Spectrum() {
    for (int column = 0; column < Width; column++) {
     barDb_[column] = peakDb_[column] = FloorDb;
    }
  }

  // Updates the bars from the last FftSize of `count` samples (-32768..32767).
  void update(const int16_t* samples, size_t count) {
    const int16_t* start = samples + count - FftSize;
    for (int i = 0; i < FftSize; i++) {
      real_[i] = start[i] / 32768.0f;
      imaginary_[i] = 0;
    }
    fft_.windowing(FFTWindow::Hann, FFTDirection::Forward);
    fft_.compute(FFTDirection::Forward);
    fft_.complexToMagnitude();  // magnitudes now in real_[0..FftSize/2)

    const float scale = 4.0f / FftSize;  // Hann window: a full-scale sine ~ 0 dBFS
    for (int column = 1; column < Width; column++) {  // column 0 is DC, never drawn
      const float db = 20.0f * log10f(real_[column] * scale + 1e-9f);
      // Fast attack, slower release keeps the bars readable.
      barDb_[column] = db > barDb_[column] ? db : barDb_[column] * 0.6f + db * 0.4f;
      peakDb_[column] = max(peakDb_[column] - 2.0f, barDb_[column]);
    }
  }

  // Draws the bars below the status line.
  void draw(Adafruit_SSD1306& display) const {
    for (int column = 1; column < Width; column++) {
      const int height = toPixels(barDb_[column]);
      if (height > 0) display.drawFastVLine(column, 64 - height, height, SSD1306_WHITE);
      const int peak = toPixels(peakDb_[column]);
      if (peak > height + 1) display.drawPixel(column, 64 - peak, SSD1306_WHITE);
    }
    // Tick marks every 1 kHz (16 columns).
    for (int column = 0; column < Width; column += 16) display.drawPixel(column, Top, SSD1306_WHITE);
  }

 private:
  static int toPixels(float db) {
    const float fraction = (db - FloorDb) / (CeilingDb - FloorDb);
    return constrain(int(fraction * (Height - 1)), 0, Height - 1);
  }

  float real_[FftSize];
  float imaginary_[FftSize];
  ArduinoFFT<float> fft_{real_, imaginary_, FftSize, float(SAMPLE_RATE)};
  float barDb_[Width];
  float peakDb_[Width];
};

// Status line on top, spectrum bars below.
inline void drawSpectrumScreen(Adafruit_SSD1306& display, const Spectrum& spectrum,
                               const String& status) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print(status);
  display.drawFastHLine(0, Spectrum::Top - 1, 128, SSD1306_WHITE);
  spectrum.draw(display);
  display.display();
}
