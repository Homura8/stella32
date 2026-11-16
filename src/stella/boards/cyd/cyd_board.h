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

namespace stella {
namespace cyd {

struct Profile {
  static constexpr const char *kName = "ESP32-2432S028R (CYD)";
  static constexpr int kWidth = 320, kHeight = 240;  // after rotation
  static constexpr uint8_t kRotation = 1;            // landscape, USB on the left
  static constexpr bool kInverted = true;            // this panel batch needs ILI9341 inversion
  static constexpr uint8_t kBacklight = 21;
  static constexpr uint8_t kTouchCs = 33, kTouchIrq = 36, kTouchSck = 25, kTouchMiso = 39, kTouchMosi = 32;
  static constexpr TouchCalibration kTouch{240, 3850, 220, 3850, false, false, false, 200};
  static constexpr int kRenderRows = 20;  // 320 x 20 x 2 B = 12.5 KiB chunk buffer
};

#if defined(TFT_BL)
static_assert(TFT_BL == Profile::kBacklight, "tft_setup.h and the CYD profile disagree on the backlight pin");
#endif

class CydDisplay : public Display {
 public:
  void begin() {
    pinMode(Profile::kBacklight, OUTPUT);
    analogWrite(Profile::kBacklight, 0);  // stay dark until the first frame
    tft_.init();
    tft_.setRotation(Profile::kRotation);
    tft_.invertDisplay(Profile::kInverted);
    tft_.setSwapBytes(true);  // native RGB565 -> panel byte order
    tft_.fillScreen(TFT_BLACK);
  }
  int16_t width() const override { return Profile::kWidth; }
  int16_t height() const override { return Profile::kHeight; }
  void writePixels(const Rect &area, const Color *pixels) override {
    tft_.pushImage(area.x, area.y, area.w, area.h, const_cast<uint16_t *>(pixels));
  }
  void setBacklight(uint8_t level) override { analogWrite(Profile::kBacklight, level); }

 private:
  TFT_eSPI tft_;
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

// Everything the runtime needs, ready to hand to Runtime::begin().
class CydBoard {
 public:
  CydBoard() : platform_(Profile::kName) {}
  void begin() {
    display_.begin();
    touch_.begin();
  }
  Board board() { return Board{&display_, &touch_, &platform_, &storage_}; }
  Color *renderBuffer() { return buffer_; }
  size_t renderBufferPixels() const { return sizeof(buffer_) / sizeof(buffer_[0]); }

 private:
  CydDisplay display_;
  CydTouch touch_;
  esp32::Esp32Platform platform_;
  esp32::NvsStore storage_;
  Color buffer_[Profile::kWidth * Profile::kRenderRows];
};

}  // namespace cyd
}  // namespace stella
