#pragma once
// Ambient light from a cheap LDR that sits right beside a backlit screen, in a
// case of any colour. Three numbers, measured on the device itself by
// Settings > Sound & light > Calibrate light sensor, make that work:
//   dark    the reading in a dark room with the backlight off,
//   self    how much the screen's own backlight adds at full brightness
//           (signed: the LDR may read higher or lower in light),
//   bright  the reading in normal room light with the backlight off.
// A reading taken with the backlight at some duty is first corrected for the
// screen's own light, then placed between dark (0) and bright (1). The sign of
// (bright - dark) is the sensor's polarity, so it needs no configuring.
#include <stdint.h>

#include "../app/system_settings.h"  // kMinLightRange

namespace stella {

struct LightCalibration {
  uint16_t dark = 0;
  int16_t self = 0;
  uint16_t bright = 0;
  bool valid() const { return bright != dark; }
};

class Ambient {
 public:
  void configure(const LightCalibration &c);
  void reset() { warm_ = false; }
  // `duty` (0..255) is the backlight PWM duty that was lit while `raw` was read.
  void sample(uint16_t raw, uint8_t duty);
  // 0..1, smoothed. 0 = dark, 1 = normal room light or brighter.
  float level() const { return level_; }
  // The fraction of the user's brightness to use: 1 in a bright room, down to
  // `kMinScale` in the dark, 0..255.
  uint8_t scale() const;
  // The instantaneous value (not smoothed), for the calibration screen.
  float instant(uint16_t raw, uint8_t duty) const;

  static constexpr float kMinScale = 0.12f;

 private:
  LightCalibration cal_;
  float level_ = 1.f;
  bool warm_ = false;
};

}  // namespace stella
