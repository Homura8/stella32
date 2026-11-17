#include "led_engine.h"

#include <math.h>

#include "../core/deadline.h"

namespace stella {

void LedEngine::configure(uint8_t mode, uint8_t levelPercent) {
  mode_ = mode > 2 ? 1 : mode;
  level_ = levelPercent < 5 ? 5 : levelPercent > 100 ? 100 : levelPercent;
}

void LedEngine::set(LedSource source, const LedRequest &request) {
  if ((int)source < (int)LedSource::kCount) requests_[(int)source] = request;
}

LedColor LedEngine::frame(uint32_t nowMs, bool &animating) const {
  animating = false;
  if (mode_ == 0) return LedColor{};
  // Most important first.
  for (int i = (int)LedSource::kCount - 1; i >= 0; --i) {
    const LedRequest &r = requests_[i];
    if (r.pattern == LedPattern::Off) continue;
    if (i == (int)LedSource::Glow && mode_ < 2) continue;  // the glow is opt-in
    if (r.untilMs && reached(nowMs, r.untilMs)) continue;  // ran out by itself
    float k = 1.f;
    switch (r.pattern) {
      case LedPattern::Steady: break;
      case LedPattern::Breathe: {
        const uint32_t period = r.periodMs ? r.periodMs : 2000;
        const float phase = (float)(nowMs % period) / (float)period;
        // 0 -> 1 -> 0, smooth, but dwelling near the top: a plain cosine spends
        // most of each breath dim, and an LED on a bedside table should be seen.
        const float wave = powf(0.5f - 0.5f * cosf(phase * 6.2831853f), 0.6f);
        const float floor = r.floor / 255.f;
        k = floor + (1.f - floor) * wave;
        animating = true;
        break;
      }
      case LedPattern::Blink: {
        const uint32_t period = r.periodMs ? r.periodMs : 1500;
        k = nowMs % period < 140 ? 1.f : 0.f;
        animating = true;
        break;
      }
      case LedPattern::Off: break;
    }
    if (r.untilMs) animating = true;  // it will end: look again
    const float scale = k * level_ / 100.f;
    LedColor out;
    out.r = (uint8_t)(r.color.r * scale + 0.5f);
    out.g = (uint8_t)(r.color.g * scale + 0.5f);
    out.b = (uint8_t)(r.color.b * scale + 0.5f);
    return out;
  }
  return LedColor{};
}

}  // namespace stella
