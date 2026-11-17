#include "ambient.h"

#include <math.h>

namespace stella {

void Ambient::configure(const LightCalibration &c) {
  cal_ = c;
  warm_ = false;
}

float Ambient::instant(uint16_t raw, uint8_t duty) const {
  if (!cal_.valid()) return 1.f;
  const float screen = (float)cal_.self * duty / 255.f;
  const float corrected = (float)raw - screen;
  const float span = (float)cal_.bright - (float)cal_.dark;  // signed: the polarity
  const float a = (corrected - (float)cal_.dark) / span;
  return a < 0.f ? 0.f : a > 1.f ? 1.f : a;
}

void Ambient::sample(uint16_t raw, uint8_t duty) {
  const float a = instant(raw, duty);
  if (!warm_) {
    level_ = a;
    warm_ = true;
    return;
  }
  // A moving average a little over a second long at four samples a second:
  // a hand passing over the sensor must not make the screen flicker.
  level_ += (a - level_) * 0.2f;
}

uint8_t Ambient::scale() const {
  // The eye takes brightness roughly logarithmically, so most of the dimming
  // happens between "dark" and "dim room", not near full light.
  const float s = kMinScale + (1.f - kMinScale) * powf(level_, 0.6f);
  return (uint8_t)(s * 255.f + 0.5f);
}

}  // namespace stella
