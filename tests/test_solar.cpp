// Sunrise and sunset against published values for well-known places and dates
// (to within three minutes), the polar cases, and the day-to-day behaviour.
#include <stdlib.h>

#include "stella/core/solar.h"
#include "test.h"

using namespace stella;

namespace {

// UTC epoch of year-month-day hh:mm (proleptic Gregorian).
int64_t utc(int y, int m, int d, int hh, int mm) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
  return days * 86400 + hh * 3600 + mm * 60;
}

bool near(int64_t actual, int64_t expected, int toleranceMinutes = 3) {
  return llabs(actual - expected) <= (int64_t)toleranceMinutes * 60;
}

}  // namespace

TEST(sunrise_and_sunset_match_published_times) {
  // London, 21 June 2026: 04:43 and 21:21 BST (UTC+1).
  SunTimes s = sunTimes(utc(2026, 6, 21, 11, 0), 51.5074f, -0.1278f);
  CHECK(s.valid && near(s.sunrise, utc(2026, 6, 21, 3, 43)) && near(s.sunset, utc(2026, 6, 21, 20, 21)));
  // New York, 21 December 2026: 07:17 and 16:32 EST (UTC-5).
  s = sunTimes(utc(2026, 12, 21, 17, 0), 40.7128f, -74.0060f);
  CHECK(s.valid && near(s.sunrise, utc(2026, 12, 21, 12, 17)) && near(s.sunset, utc(2026, 12, 21, 21, 32)));
  // Melbourne, 21 December 2026: about 05:54 and 20:41 AEDT (UTC+11), across the UTC date line. (Checked by
  // hand: solar noon 13:18 AEDT, half a day of 7 h 24 min at this latitude and declination.)
  s = sunTimes(utc(2026, 12, 21, 1, 0), -37.8136f, 144.9631f);
  CHECK(s.valid && near(s.sunrise, utc(2026, 12, 20, 18, 54)) && near(s.sunset, utc(2026, 12, 21, 9, 41)));
  // Sydney, 21 June 2026: 07:00 and 16:54 AEST (UTC+10).
  s = sunTimes(utc(2026, 6, 21, 2, 0), -33.8688f, 151.2093f);
  CHECK(s.valid && near(s.sunrise, utc(2026, 6, 20, 21, 0), 4) && near(s.sunset, utc(2026, 6, 21, 6, 54), 4));
  // The equator at the March equinox: about 06:07 and 18:10 local solar time.
  s = sunTimes(utc(2026, 3, 20, 12, 0), 0.f, 0.f);
  CHECK(s.valid && near(s.sunrise, utc(2026, 3, 20, 6, 7)) && near(s.sunset, utc(2026, 3, 20, 18, 10)));
}

TEST(polar_day_and_polar_night_have_no_sunrise) {
  // Tromso (69.65 N): the Sun does not set in midsummer and does not rise in midwinter.
  SunTimes s = sunTimes(utc(2026, 6, 21, 12, 0), 69.6492f, 18.9553f);
  CHECK(!s.valid && s.polarDay && !s.polarNight);
  s = sunTimes(utc(2026, 12, 21, 12, 0), 69.6492f, 18.9553f);
  CHECK(!s.valid && s.polarNight && !s.polarDay);
  // Southern hemisphere: Antarctica in December has polar day.
  s = sunTimes(utc(2026, 12, 21, 12, 0), -80.f, 0.f);
  CHECK(s.polarDay);
}

TEST(days_lengthen_toward_midsummer_and_shift_smoothly) {
  float lastLength = 0;
  for (int day = 0; day < 20; ++day) {  // the northern spring: days get longer
    const SunTimes s = sunTimes(utc(2026, 3, 1 + day, 12, 0), 51.5f, 0.f);
    CHECK(s.valid && s.sunset > s.sunrise);
    const float length = (float)(s.sunset - s.sunrise);
    CHECK(length > lastLength);
    lastLength = length;
  }
  // The same place, a day apart: the times move by minutes, not hours.
  const SunTimes a = sunTimes(utc(2026, 9, 10, 12, 0), 48.85f, 2.35f);
  const SunTimes b = sunTimes(utc(2026, 9, 11, 12, 0), 48.85f, 2.35f);
  CHECK(llabs((b.sunrise - a.sunrise) - 86400) < 4 * 60);
  CHECK(llabs((b.sunset - a.sunset) - 86400) < 4 * 60);
}
