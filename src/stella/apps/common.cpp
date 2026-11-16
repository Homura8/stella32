#include "common.h"

#include <math.h>
#include <stdio.h>

namespace stella {
namespace apps {

const char *weekdayName(int weekday) {
  static const char *const kNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  return kNames[(weekday % 7 + 7) % 7];
}

const char *monthName(int month) {
  static const char *const kNames[] = {"January", "February", "March",     "April",   "May",      "June",
                                       "July",    "August",   "September", "October", "November", "December"};
  return kNames[((month - 1) % 12 + 12) % 12];
}

const char *greetingFor(const LocalTime &t) {
  if (!t.valid) return "Hello";
  if (t.hour >= 5 && t.hour < 12) return "Good morning";
  if (t.hour >= 12 && t.hour < 17) return "Good afternoon";
  if (t.hour >= 17 && t.hour < 22) return "Good evening";
  return "Good night";
}

MoonPhase moonPhase(int64_t epochSeconds) {
  static const char *const kNames[] = {"New moon",  "Waxing crescent", "First quarter", "Waxing gibbous",
                                       "Full moon", "Waning gibbous",  "Last quarter",  "Waning crescent"};
  const double synodic = 29.53058867;
  const double knownNewMoon = 947182440.0;  // 2000-01-06 18:14 UTC
  double phase = fmod((epochSeconds - knownNewMoon) / 86400.0, synodic);
  if (phase < 0) phase += synodic;
  const uint8_t index = (uint8_t)((int)floor(phase / synodic * 8 + 0.5) % 8);
  const double angle = phase / synodic * 2 * 3.14159265358979;
  return MoonPhase{index, kNames[index], (float)((1 - cos(angle)) / 2)};
}

void formatLongDate(const LocalTime &t, char *out, size_t capacity) {
  if (!t.valid) {
    snprintf(out, capacity, "Waiting for the time");
    return;
  }
  snprintf(out, capacity, "%s %d %s", weekdayName(t.weekday), t.day, monthName(t.month));
}

}  // namespace apps
}  // namespace stella
