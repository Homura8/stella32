#pragma once
// Small helpers shared by the built-in apps.
#include <stddef.h>
#include <stdint.h>

#include "../app/app.h"

namespace stella {
namespace apps {

const char *weekdayName(int weekday);  // 0 = Sunday
const char *monthName(int month);      // 1 = January
const char *greetingFor(const LocalTime &t);  // "Good morning" ...

struct MoonPhase {
  uint8_t index;       // 0 new .. 4 full .. 7 waning crescent
  const char *name;
  float illumination;  // 0..1
};
// Same synodic model as the Celestia Glow site.
MoonPhase moonPhase(int64_t epochSeconds);

// "25 min", "9 h 5 min", "2 d 3 h".
void describeWait(int minutes, char *out, size_t capacity);

// "Tuesday 16 November" or, with dateOrder 1, "Tuesday November 16" (or a
// placeholder when the time is unknown).
void formatLongDate(const LocalTime &t, char *out, size_t capacity, uint8_t dateOrder = 0);
// "Tue 16 Nov" / "Tue Nov 16".
void formatShortDate(const LocalTime &t, char *out, size_t capacity, uint8_t dateOrder = 0);

// The next sun event as a short line: "Sunrise 05:54" before sunrise, "Sunset
// 20:41" until sunset and "Sunrise 05:53" after it (tomorrow's is within two
// minutes of today's). Empty when the place's sun is unknown or the time is.
void describeNextSun(AppContext &ctx, char *out, size_t capacity);

// A temperature as shown: degrees Celsius, or the nearest degree Fahrenheit.
int displayTemperature(int celsius, bool fahrenheit);

// Day order for week views: the weekday (0 = Sunday) shown in column `i` (0..6).
inline int weekdayInColumn(int i, bool weekStartsSunday) { return weekStartsSunday ? i % 7 : (i + 1) % 7; }

}  // namespace apps
}  // namespace stella
