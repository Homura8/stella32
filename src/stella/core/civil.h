#pragma once
// Calendar arithmetic on plain numbers: no time zone, no libc, no tables.
// "Wall seconds" are the seconds since 1970-01-01 00:00 of a date and time read
// off a clock, as if that clock were UTC. Dates are day numbers (days since
// 1970-01-01). Algorithms after Howard Hinnant's public-domain date library.
#include <stdint.h>

namespace stella {
namespace civil {

inline int64_t floorDiv(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }
inline int64_t floorMod(int64_t a, int64_t b) { return a - floorDiv(a, b) * b; }

inline bool isLeap(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

inline int daysInMonth(int year, int month) {
  static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  return month == 2 && isLeap(year) ? 29 : kDays[month - 1];
}

// Day number of a date (month 1-12, day 1-31).
inline int64_t daysFromCivil(int year, int month, int day) {
  const int64_t y = year - (month <= 2 ? 1 : 0);
  const int64_t era = floorDiv(y, 400);
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

inline void civilFromDays(int64_t days, int &year, int &month, int &day) {
  const int64_t z = days + 719468;
  const int64_t era = floorDiv(z, 146097);
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  day = (int)(doy - (153 * mp + 2) / 5 + 1);
  month = (int)(mp < 10 ? mp + 3 : mp - 9);
  year = (int)(yoe + era * 400 + (month <= 2 ? 1 : 0));
}

// 0 = Sunday ... 6 = Saturday.
inline int weekday(int64_t days) { return (int)floorMod(days + 4, 7); }

inline int64_t wallSeconds(int year, int month, int day, int hour, int minute, int second) {
  return daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
}

// The day number of the n-th `wd` (0 = Sunday) of a month: n = 1..5, or -1 for
// the last one, -2 the one before, and so on. Returns false if the month has no
// such day (a fifth Friday that does not exist).
inline bool nthWeekday(int year, int month, int n, int wd, int64_t &days) {
  const int dim = daysInMonth(year, month);
  const int64_t first = daysFromCivil(year, month, 1);
  if (n > 0) {
    const int offset = (int)floorMod(wd - weekday(first), 7);
    const int day = 1 + offset + (n - 1) * 7;
    if (day > dim) return false;
    days = first + day - 1;
    return true;
  }
  if (n < 0) {
    const int64_t last = first + dim - 1;
    const int back = (int)floorMod(weekday(last) - wd, 7);
    const int day = dim - back + (n + 1) * 7;
    if (day < 1) return false;
    days = first + day - 1;
    return true;
  }
  return false;
}

}  // namespace civil
}  // namespace stella
