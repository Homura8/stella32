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
#include "../../port/esp32/esp32_hid.h"  // STELLA_HAS_BLE: 1 when the NimBLE-Arduino library is present
#include "../../port/esp32/esp32_port.h"

// DMA display transfers with double-buffered rendering: the next chunk is
// drawn while the previous one is sent. Costs a second 12.5 KiB buffer.
// Off until verified on hardware; define STELLA_CYD_DMA 1 before including.
#ifndef STELLA_CYD_DMA
#define STELLA_CYD_DMA 0
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
  // X raw range shifted by 79 counts (+7 px) after testing on a CYD with its
  // stylus: touches landed ~7 px left. Touch test > Calibrate refines it.
  static constexpr TouchCalibration kTouch{161, 3771, 220, 3850, false, false, false, 200};
  // ILI9341 values the TFT_eSPI ILI9341_2 init sets; PanelTrim is relative to them.
  static constexpr int kVcomVmf = 0x37;      // VMCTR2 (0xC7) = 0x80 | VMF
  static constexpr uint8_t kFrameRtna = 0x1A;  // FRMCTR1 (0xB1): 73 Hz
  static constexpr int kRenderRows = 20;  // 320 x 20 x 2 B = 12.5 KiB chunk buffer
  static constexpr uint8_t kSpeaker = 26;  // "SPEAK" connector, through the on-board amplifier
  // The LDR (ambient light), on an input-only ADC1 pin (ADC1 works with Wi-Fi on).
  static constexpr uint8_t kLdr = 34;
  // The back RGB LED is red 4, green 16, blue 17, active-low on the board this
  // was written for; other CYD makers differ, so these are only the *default*
  // wiring (CYD.ino) and the pins that "Find my LED" may probe. Nothing else
  // is ever driven: the display, touch, SD, speaker and backlight pins are
  // not in this list.
  static constexpr uint8_t kLedCandidates[3] = {4, 16, 17};
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

  bool supportsPanelTrim() const override { return true; }
  void setPanelTrim(const PanelTrim &trim) override {
    int vmf = Profile::kVcomVmf + trim.vcom;
    vmf = vmf < 0 ? 0 : vmf > 0x7F ? 0x7F : vmf;
    tft_.writecommand(0xC7);  // VCOM control 2: offset of the common voltage
    tft_.writedata((uint8_t)(0x80 | vmf));
    tft_.writecommand(0xB1);  // frame rate control (normal mode)
    tft_.writedata(0x00);
    tft_.writedata(frameRtna(trim.refreshHz));
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

  // RTNA for the frame rate nearest `hz` (ILI9341 datasheet, DIVA = 0).
  static uint8_t frameRtna(uint8_t hz) {
    if (hz == 0) return Profile::kFrameRtna;
    static const uint8_t kHz[] = {119, 112, 106, 100, 95, 90, 86, 83, 79, 76, 73, 70};
    int best = 0;
    for (int i = 1; i < 12; ++i)
      if (abs(kHz[i] - hz) < abs(kHz[best] - hz)) best = i;
    return (uint8_t)(0x10 + best);
  }
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
// speaker is plugged in. Volume is the pulse width: a narrow pulse has a weak
// fundamental, a 50 % duty (512 of 1023) is the loudest this output can be.
// The curve is quadratic so the middle of the slider is a comfortable level.
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
    ledcWrite(Profile::kSpeaker, duty());
  }
  void setVolume(uint8_t percent) override {
    volume_ = percent > 100 ? 100 : percent;
    if (attached_ && current_) ledcWrite(Profile::kSpeaker, duty());
  }
  // 0 for a silent volume, else 4 .. 512.
  static uint32_t dutyFor(uint8_t percent) {
    return percent == 0 ? 0u : 4u + 508u * percent * percent / 10000u;
  }

 private:
  uint32_t duty() const { return dutyFor(volume_); }
  bool attached_ = false;
  uint16_t current_ = 0;
  uint8_t volume_ = 60;
};

// The back RGB LED through LEDC PWM (smooth fades), wired however the board
// maker did: the wiring is data (LedWiring), found on the device by
// Settings > Sound & light > Find my LED.
class CydLed : public Led {
 public:
  bool configure(const LedWiring &w) override {
    release();
    wiring_ = w;
    bool ok = true;
    for (int i = 0; i < 3; ++i) {
      const uint8_t pin = w.pin[i];
      if (pin == kNoPin) continue;
      if (!candidate(pin) || !ledcAttach(pin, 5000, 8)) {
        ok = false;
        continue;
      }
      attached_[i] = true;
      write(i, 0);
    }
    return ok;
  }
  void set(uint8_t r, uint8_t g, uint8_t b) override {
    write(0, r);
    write(1, g);
    write(2, b);
  }
  int candidatePins(uint8_t *out, int capacity) const override {
    int n = 0;
    for (uint8_t p : Profile::kLedCandidates)
      if (n < capacity) out[n++] = p;
    return n;
  }
  void probe(uint8_t pin, bool activeLow) override {
    release();  // plain GPIO for the test
    for (uint8_t p : Profile::kLedCandidates) {
      if (p == pin) {
        pinMode(p, OUTPUT);
        digitalWrite(p, activeLow ? LOW : HIGH);
      } else {
        pinMode(p, INPUT);  // high impedance: an LED on it cannot light
      }
    }
  }

 private:
  static bool candidate(uint8_t pin) {
    for (uint8_t p : Profile::kLedCandidates)
      if (p == pin) return true;
    return false;
  }
  void release() {
    for (int i = 0; i < 3; ++i) {
      if (attached_[i]) ledcDetach(wiring_.pin[i]);
      attached_[i] = false;
    }
  }
  void write(int i, uint8_t v) {
    if (attached_[i]) ledcWrite(wiring_.pin[i], wiring_.inverted ? 255 - v : v);
  }
  LedWiring wiring_;
  bool attached_[3] = {false, false, false};
};

// The LDR beside the screen: a few ADC samples averaged. What the number means
// (and which way round it goes) is learned by the calibration.
class CydLight : public LightSensor {
 public:
  uint16_t read() override {
    uint32_t sum = 0;
    for (int i = 0; i < 16; ++i) sum += analogRead(Profile::kLdr);
    return (uint16_t)(sum / 16);
  }
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
  Board board() {
    Board b{&display_, &touch_, &platform_, &storage_, &wifi_, &http_, &sound_, &setup_, &firmware_, &led_, &light_, &remote_, &wallpaper_};
#if STELLA_HAS_BLE
    b.hid = &hid_;
#endif
    return b;
  }
  // The ECDSA P-256 public key (PEM) that update manifests must be signed with.
  void setUpdateKey(const char *pem) { firmware_.setPublicKey(pem); }
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
  CydLed led_;
  CydLight light_;
  esp32::Esp32RemoteServer remote_;
  esp32::Esp32WallpaperStore wallpaper_;
#if STELLA_HAS_BLE
  esp32::Esp32Hid hid_;
#endif
  esp32::Esp32SetupServer setup_;
  esp32::Esp32Firmware firmware_;
  Color buffer_[Profile::kWidth * Profile::kRenderRows];
#if STELLA_CYD_DMA
  Color buffer2_[Profile::kWidth * Profile::kRenderRows];
#endif
};

}  // namespace cyd
}  // namespace stella
