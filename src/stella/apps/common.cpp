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

const char *greetingShortFor(const LocalTime &t) {
  if (!t.valid) return "Hello";
  if (t.hour >= 5 && t.hour < 12) return "Morning";
  if (t.hour >= 12 && t.hour < 17) return "Afternoon";
  if (t.hour >= 17 && t.hour < 22) return "Evening";
  return "Night";
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

void formatLongDate(const LocalTime &t, char *out, size_t capacity, uint8_t dateOrder) {
  if (!t.valid) {
    snprintf(out, capacity, "Waiting for the time");
    return;
  }
  if (dateOrder == 1) snprintf(out, capacity, "%s %s %d", weekdayName(t.weekday), monthName(t.month), t.day);
  else snprintf(out, capacity, "%s %d %s", weekdayName(t.weekday), t.day, monthName(t.month));
}

void formatShortDate(const LocalTime &t, char *out, size_t capacity, uint8_t dateOrder) {
  if (!t.valid) {
    snprintf(out, capacity, "--");
    return;
  }
  if (dateOrder == 1) snprintf(out, capacity, "%.3s %.3s %d", weekdayName(t.weekday), monthName(t.month), t.day);
  else snprintf(out, capacity, "%.3s %d %.3s", weekdayName(t.weekday), t.day, monthName(t.month));
}

void describeNextSun(AppContext &ctx, char *out, size_t capacity) {
  out[0] = '\0';
  const SystemStatus st = ctx.status();
  const LocalTime now = ctx.localTime();
  if (!now.valid) return;
  if (st.sunPolar == 1) return (void)snprintf(out, capacity, "The Sun stays up today");
  if (st.sunPolar == 2) return (void)snprintf(out, capacity, "The Sun stays down today");
  if (st.sunrise == 0 || st.sunset == 0) return;
  const bool rise = now.epoch < st.sunrise || now.epoch >= st.sunset;
  const LocalTime at = ctx.localTimeAt(rise ? st.sunrise : st.sunset);
  char clock[12];
  ctx.formatTime(at, clock, sizeof(clock));
  snprintf(out, capacity, "%s %s%s", rise ? "Sunrise" : "Sunset", clock,
           ctx.settings().clock24h ? "" : at.hour < 12 ? " AM" : " PM");
}

int displayTemperature(int celsius, bool fahrenheit) {
  if (!fahrenheit) return celsius;
  const int tenths = celsius * 18;  // nearest degree: round half away from zero
  return (tenths + (tenths >= 0 ? 5 : -5)) / 10 + 32;
}

void describeWait(int minutes, char *out, size_t capacity) {
  if (minutes < 60) snprintf(out, capacity, "%d min", minutes < 1 ? 1 : minutes);
  else if (minutes < 1440) {
    if (minutes % 60) snprintf(out, capacity, "%d h %d min", minutes / 60, minutes % 60);
    else snprintf(out, capacity, "%d h", minutes / 60);
  } else {
    snprintf(out, capacity, "%d d %d h", minutes / 1440, (minutes % 1440) / 60);
  }
}

}  // namespace apps
}  // namespace stella
