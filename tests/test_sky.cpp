// The sky follows the real sun: sunrise and sunset from the Weather place
// (core/solar) decide dawn, day, dusk and night, with the fixed hours as the
// fallback and the polar cases handled.
#include "sim_session.h"
#include "stella/ui/theme.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {
// Melbourne, local time (UTC+10 in winter, +11 in summer): epoch of a local clock reading.
int64_t melbourne(int year, int month, int day, int hour, int minute, int offsetHours) {
  int y = year - (month <= 2);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
  return days * 86400 + (hour - offsetHours) * 3600 + minute * 60;
}

SkyPhase phaseAt(int64_t epoch) {
  SimSession s(320, 240, epoch);
  s.run(500);  // the Weather app tells the shell about today's sun
  return s.os.theme().phase;
}
}  // namespace

TEST(phase_for_sun_follows_sunrise_and_sunset) {
  const int64_t rise = 1000000, set = rise + 14 * 3600;
  CHECK(phaseForSun(rise - 60 * 60, rise, set) == SkyPhase::Night);        // an hour before sunrise
  CHECK(phaseForSun(rise - 30 * 60, rise, set) == SkyPhase::Dawn);         // 45 min before: dawn begins
  CHECK(phaseForSun(rise + 60 * 60, rise, set) == SkyPhase::Dawn);
  CHECK(phaseForSun(rise + 80 * 60, rise, set) == SkyPhase::Day);          // 75 min after sunrise
  CHECK(phaseForSun(set - 70 * 60, rise, set) == SkyPhase::Day);
  CHECK(phaseForSun(set - 50 * 60, rise, set) == SkyPhase::Dusk);          // the last hour of light
  CHECK(phaseForSun(set + 30 * 60, rise, set) == SkyPhase::Dusk);
  CHECK(phaseForSun(set + 50 * 60, rise, set) == SkyPhase::Night);
}

TEST(the_sky_follows_the_real_sun_in_summer_and_winter) {
  // Summer (16 Nov, sunrise ~05:59, sunset ~20:10 AEDT): 20:20 is still dusk, 05:00 still night
  // (the old fixed hours said night from 20:00 and dawn from 05:00).
  CHECK(phaseAt(melbourne(2026, 11, 16, 20, 20, 11)) == SkyPhase::Dusk);
  CHECK(phaseAt(melbourne(2026, 11, 16, 5, 0, 11)) == SkyPhase::Night);
  CHECK(phaseAt(melbourne(2026, 11, 16, 6, 30, 11)) == SkyPhase::Dawn);
  CHECK(phaseAt(melbourne(2026, 11, 16, 13, 0, 11)) == SkyPhase::Day);
  CHECK(phaseAt(melbourne(2026, 11, 16, 21, 30, 11)) == SkyPhase::Night);
  // Winter (21 June, sunrise ~07:35, sunset ~17:05 AEST): dark by 18:00, light only from ~07:00
  // (the old fixed hours said dusk until 20:00 and dawn from 05:00).
  CHECK(phaseAt(melbourne(2026, 6, 21, 19, 30, 10)) == SkyPhase::Night);
  CHECK(phaseAt(melbourne(2026, 6, 21, 6, 0, 10)) == SkyPhase::Night);
  CHECK(phaseAt(melbourne(2026, 6, 21, 7, 15, 10)) == SkyPhase::Dawn);
  CHECK(phaseAt(melbourne(2026, 6, 21, 12, 0, 10)) == SkyPhase::Day);
  CHECK(phaseAt(melbourne(2026, 6, 21, 17, 30, 10)) == SkyPhase::Dusk);
}

TEST(night_status_and_dimming_follow_the_sun_too) {
  SimSession late(320, 240, melbourne(2026, 11, 16, 20, 20, 11));  // dusk: not night yet
  late.run(500);
  CHECK(!late.os.status().night);
  SimSession later(320, 240, melbourne(2026, 11, 16, 21, 30, 11));
  later.run(500);
  CHECK(later.os.status().night);
}

TEST(polar_sun_and_unknown_sun_override_or_fall_back) {
  SimSession s(320, 240, melbourne(2026, 11, 16, 13, 0, 11));
  s.run(500);
  CHECK(s.os.theme().phase == SkyPhase::Day);
  s.os.setSun(0, 0, 2);  // the Sun stays down all day
  CHECK(s.os.theme().phase == SkyPhase::Night && s.os.status().night);
  s.os.setSun(0, 0, 1);  // stays up all day
  CHECK(s.os.theme().phase == SkyPhase::Day);
  // Unknown (no time or no place): back to the fixed hours (13:00 is day either way).
  s.os.setSun(0, 0, 0);
  CHECK(s.os.theme().phase == SkyPhase::Day);
  s.dev.platform.setEpoch(melbourne(2026, 11, 16, 20, 20, 11));  // fixed hours: night from 20:00
  s.run(100);
  s.os.setSun(0, 0, 0);
  CHECK(s.os.theme().phase == SkyPhase::Night);
}

TEST(stale_sun_times_are_ignored) {
  SimSession s(320, 240, melbourne(2026, 11, 16, 13, 0, 11));
  s.run(500);
  // Yesterday's sun from two days ago, told to the shell: a day-old report must not decide today.
  const int64_t old = melbourne(2026, 11, 14, 6, 0, 11);
  s.os.setSun(old, old + 14 * 3600, 0);
  s.dev.platform.setEpoch(melbourne(2026, 11, 16, 2, 0, 11));
  s.os.setSun(old + 60, old + 14 * 3600, 0);  // (a different value, so the shell looks again)
  CHECK(s.os.theme().phase == SkyPhase::Night);  // the fixed hours decide at 02:00
}
