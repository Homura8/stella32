#pragma once
// What the status LED shows. Several things may want it at once (an alarm
// ringing, an update downloading, the bedside glow): each is a *source* with
// its own pattern, and the most important active one wins. This is only the
// arithmetic (a colour for a given time); the runtime feeds the sources and
// writes the result to the board's Led.
#include <stdint.h>

namespace stella {

struct LedColor {
  uint8_t r = 0, g = 0, b = 0;
  bool operator==(const LedColor &o) const { return r == o.r && g == o.g && b == o.b; }
  bool operator!=(const LedColor &o) const { return !(*this == o); }
};

enum class LedSource : uint8_t {
  Glow,    // the faint bedside glow
  Wifi,    // phone setup open, a join succeeding or failing
  Update,  // downloading, ready, failed
  Flash,   // a short blink an app asked for
  Alert,   // an alarm or timer ringing
  kCount,
};

enum class LedPattern : uint8_t {
  Off,
  Steady,
  Breathe,  // fades up and down smoothly over `periodMs`
  Blink,    // on for a short moment once per `periodMs`
};

struct LedRequest {
  LedPattern pattern = LedPattern::Off;
  LedColor color;
  uint16_t periodMs = 2000;
  uint8_t floor = 0;       // Breathe never goes dimmer than this (0..255 of the colour)
  uint32_t untilMs = 0;    // 0 = until cleared; else it ends by itself (wrap-safe)
};

class LedEngine {
 public:
  // The brightest it ever gets, 5..100 %, and the mode (0 off, 1 alerts and
  // progress, 2 also the glow).
  void configure(uint8_t mode, uint8_t levelPercent);
  void set(LedSource source, const LedRequest &request);
  void clear(LedSource source) { set(source, LedRequest{}); }
  // The colour to show at `nowMs`. `animating` is true while it will change
  // from one moment to the next (the runtime then asks again soon).
  LedColor frame(uint32_t nowMs, bool &animating) const;

 private:
  LedRequest requests_[(int)LedSource::kCount];
  uint8_t mode_ = 1, level_ = 60;
};

}  // namespace stella
