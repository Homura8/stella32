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

// "Tuesday 16 November" (or a placeholder when the time is unknown).
void formatLongDate(const LocalTime &t, char *out, size_t capacity);

}  // namespace apps
}  // namespace stella
