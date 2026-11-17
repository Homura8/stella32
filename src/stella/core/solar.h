#pragma once
// Sunrise and sunset from a place and a date, computed on the device (no
// network, no tables): the "sunrise equation" with the usual 0.833 degree
// correction for the Sun's size and atmospheric refraction. Good to about two
// minutes away from the poles, which is far finer than anything shown on a
// 320x240 clock.
#include <stdint.h>

namespace stella {

struct SunTimes {
  bool valid = false;       // false: no sunrise or sunset that day (see polar*)
  bool polarDay = false;    // the Sun stays up all day
  bool polarNight = false;  // the Sun stays down all day
  int64_t sunrise = 0;      // epoch seconds (UTC)
  int64_t sunset = 0;
};

// `localNoon` is any UTC epoch that falls at local noon of the date wanted
// (local midnight plus 12 h is fine); it only picks the day. Latitude north
// positive, longitude east positive, degrees.
SunTimes sunTimes(int64_t localNoon, float latitude, float longitude);

}  // namespace stella
