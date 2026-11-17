#pragma once
// Board support: common ESP32-2432S028R "Cheap Yellow Display" (2.8" 320x240
// ILI9341 on HSPI, XPT2046 resistive touch on VSPI, no PSRAM).
//
// Header-only and compiled inside the sketch so TFT_eSPI sees the sketch's
// tft_setup.h (examples/CYD/tft_setup.h). Pin and calibration values come
// from Celestia Nightstand, where they were used on this hardware.
//
// Adding another board: copy this folder, change the profile and drivers,
// add an example sketch with its own tft_setup.h. Nothing above the HAL
// changes; layouts adapt to the panel size through the theme's density class.
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include "../../hal/hal.h"
#include "../../input/touch_calibration.h"
#include "../../port/esp32/esp32_port.h"

// DMA display transfers with double-buffered rendering: the next chunk is
// drawn while the previous one is sent. Costs a second 12.5 KiB buffer.
// Off until verified on hardware; define STELLA_CYD_DMA 1 before including.
#ifndef STELLA_CYD_DMA
#define STELLA_CYD_DMA 0
#endif

// Alert chime loudness: PWM duty out of 1023 on the speaker pin. A narrow
// pulse has a weaker fundamental, so small values are gentler (512 = loudest).
#ifndef STELLA_CYD_TONE_DUTY
#define STELLA_CYD_TONE_DUTY 64
#endif

namespace stella {
namespace cyd {

struct Profile {
  static constexpr const char *kName = "ESP32-2432S028R (CYD)";
  static constexpr int kWidth = 320, kHeight = 240;  // after rotation
  static constexpr uint8_t kRotation = 1;            // landscape, USB on the left
  static constexpr bool kInverted = true;            // this panel batch needs ILI9341 inversion
  static constexpr uint8_t kBacklight = 21;
  // Backlight PWM. analogWrite()'s 1 kHz default can be seen as flicker;
  // 20 kHz is far above it and still inaudible.
  static constexpr uint32_t kBacklightHz = 20000;
  static constexpr uint8_t kTouchCs = 33, kTouchIrq = 36, kTouchSck = 25, kTouchMiso = 39, kTouchMosi = 32;
  static constexpr TouchCalibration kTouch{240, 3850, 220, 3850, false, false, false, 200};
  static constexpr int kRenderRows = 20;  // 320 x 20 x 2 B = 12.5 KiB chunk buffer
  static constexpr uint8_t kSpeaker = 26;  // "SPEAK" connector, through the on-board amplifier
};

#if defined(TFT_BL)
#error "Do not define TFT_BL for the CYD: CydDisplay drives the backlight with high-frequency PWM"
#endif

class CydDisplay : public Display {
 public:
  void begin() {
    pinMode(Profile::kBacklight, OUTPUT);
    digitalWrite(Profile::kBacklight, LOW);  // stay dark until the first frame
    pwm_ = ledcAttach(Profile::kBacklight, Profile::kBacklightHz, 8);
    if (pwm_) ledcWrite(Profile::kBacklight, 0);
    tft_.init();
    tft_.setRotation(Profile::kRotation);
    tft_.invertDisplay(Profile::kInverted);
    tft_.setSwapBytes(true);  // native RGB565 -> panel byte order
    tft_.fillScreen(TFT_BLACK);
#if STELLA_CYD_DMA
    dma_ = tft_.initDMA();
#endif
  }
  int16_t width() const override { return Profile::kWidth; }
  int16_t height() const override { return Profile::kHeight; }
  void writePixels(const Rect &area, const Color *pixels) override {
    tft_.pushImage(area.x, area.y, area.w, area.h, const_cast<uint16_t *>(pixels));
  }
  void setBacklight(uint8_t level) override {
    if (pwm_) ledcWrite(Profile::kBacklight, level);
    else analogWrite(Profile::kBacklight, level);  // fallback: 1 kHz
  }

  bool supportsAsync() const override { return dma_; }
  void writePixelsAsync(const Rect &area, Color *pixels) override {
#if STELLA_CYD_DMA
    if (dma_) {
      if (!inTransaction_) {
        tft_.startWrite();
        inTransaction_ = true;
      }
      // Waits for the previous DMA transfer, byte-swaps `pixels` in place
      // (allowed by the Display contract), then starts this one.
      tft_.pushImageDMA(area.x, area.y, area.w, area.h, pixels);
      return;
    }
#endif
    writePixels(area, pixels);
  }
  void waitIdle() override {
#if STELLA_CYD_DMA
    if (inTransaction_) {
      tft_.dmaWait();
      tft_.endWrite();
      inTransaction_ = false;
    }
#endif
  }

 private:
  TFT_eSPI tft_;
  bool dma_ = false;
  bool inTransaction_ = false;
  bool pwm_ = false;
};

class CydTouch : public TouchInput {
 public:
  CydTouch() : spi_(VSPI), touch_(Profile::kTouchCs, Profile::kTouchIrq) {}
  void begin() {
    spi_.begin(Profile::kTouchSck, Profile::kTouchMiso, Profile::kTouchMosi, Profile::kTouchCs);
    touch_.begin(spi_);
    touch_.setRotation(Profile::kRotation);
  }
  bool sample(TouchSample &out) override {
    out = TouchSample{};
    if (!touch_.touched()) return true;
    const TS_Point p = touch_.getPoint();
    int x = 0, y = 0;
    out.rawX = p.x;
    out.rawY = p.y;
    out.pressure = (uint16_t)(p.z < 0 ? 0 : p.z);
    if (mapTouch(Profile::kTouch, p.x, p.y, p.z, Profile::kWidth, Profile::kHeight, x, y)) {
      out.pressed = true;
      out.x = (int16_t)x;
      out.y = (int16_t)y;
    }
    return true;
  }

 private:
  SPIClass spi_;
  XPT2046_Touchscreen touch_;
};

// Square-wave tones on the speaker connector (LEDC PWM). Silent unless a
// speaker is plugged in.
class CydSound : public Sound {
 public:
  void begin() { attached_ = ledcAttach(Profile::kSpeaker, 2000, 10); }
  void tone(uint16_t hz) override {
    if (!attached_ || hz == current_) return;
    current_ = hz;
    if (hz == 0) {
      ledcWrite(Profile::kSpeaker, 0);
      return;
    }
    ledcChangeFrequency(Profile::kSpeaker, hz, 10);
    ledcWrite(Profile::kSpeaker, STELLA_CYD_TONE_DUTY);
  }

 private:
  bool attached_ = false;
  uint16_t current_ = 0;
};

// Everything the runtime needs, ready to hand to Runtime::begin().
class CydBoard {
 public:
  CydBoard() : platform_(Profile::kName) {}
  void begin() {
    display_.begin();
    touch_.begin();
    sound_.begin();
  }
  Board board() { return Board{&display_, &touch_, &platform_, &storage_, &wifi_, &http_, &sound_}; }
  Color *renderBuffer() { return buffer_; }
  size_t renderBufferPixels() const { return sizeof(buffer_) / sizeof(buffer_[0]); }
#if STELLA_CYD_DMA
  Color *secondRenderBuffer() { return buffer2_; }
#else
  Color *secondRenderBuffer() { return nullptr; }
#endif

 private:
  CydDisplay display_;
  CydTouch touch_;
  esp32::Esp32Platform platform_;
  esp32::NvsStore storage_;
  esp32::Esp32WifiDriver wifi_;
  esp32::Esp32HttpClient http_;
  CydSound sound_;
  Color buffer_[Profile::kWidth * Profile::kRenderRows];
#if STELLA_CYD_DMA
  Color buffer2_[Profile::kWidth * Profile::kRenderRows];
#endif
};

}  // namespace cyd
}  // namespace stella
