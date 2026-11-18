#include "posix_zone.h"

#include <string.h>

namespace stella {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// A zone name: three or more letters, or anything inside <...>.
bool skipName(const char *&p) {
  if (*p == '<') {
    while (*p && *p != '>') ++p;
    if (*p != '>') return false;
    ++p;
    return true;
  }
  const char *start = p;
  while ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) ++p;
  return p - start >= 3;
}

// [+-]hh[:mm[:ss]] in seconds, or false.
bool readTime(const char *&p, int32_t &seconds) {
  int sign = 1;
  if (*p == '+' || *p == '-') sign = *p++ == '-' ? -1 : 1;
  if (!isDigit(*p)) return false;
  int part[3] = {0, 0, 0};
  for (int i = 0; i < 3; ++i) {
    int v = 0, digits = 0;
    while (isDigit(*p) && digits < 3) {
      v = v * 10 + (*p++ - '0');
      ++digits;
    }
    if (digits == 0) return false;
    part[i] = v;
    if (*p != ':') break;
    ++p;
  }
  if (part[0] > 167 || part[1] > 59 || part[2] > 59) return false;
  seconds = sign * (part[0] * 3600 + part[1] * 60 + part[2]);
  return true;
}

// Mm.w.d[/time]
bool readRule(const char *&p, uint8_t &month, int8_t &nth, int8_t &weekday, int32_t &timeOfDay) {
  if (*p != 'M') return false;
  ++p;
  int m = 0, w = 0, d = 0;
  auto number = [&](int &out) {
    if (!isDigit(*p)) return false;
    out = 0;
    while (isDigit(*p)) out = out * 10 + (*p++ - '0');
    return true;
  };
  if (!number(m) || *p++ != '.' || !number(w) || *p++ != '.' || !number(d)) return false;
  if (m < 1 || m > 12 || w < 1 || w > 5 || d < 0 || d > 6) return false;
  month = (uint8_t)m;
  nth = (int8_t)(w == 5 ? -1 : w);
  weekday = (int8_t)d;
  timeOfDay = 2 * 3600;
  if (*p == '/') {
    ++p;
    if (!readTime(p, timeOfDay)) return false;
  }
  return true;
}

}  // namespace

bool parsePosixZone(const char *tz, ICalZone &zone) {
  zone = ICalZone();
  zone.standard.present = true;  // UTC until something better is understood
  if (!tz || !*tz) return false;
  const char *p = tz;
  int32_t stdOffset;
  if (!skipName(p) || !readTime(p, stdOffset)) return false;
  const int32_t stdUtc = -stdOffset;  // POSIX counts west of Greenwich
  if (!*p) {
    zone.standard.offset = stdUtc;
    zone.standard.offsetFrom = stdUtc;
    return true;
  }
  if (!skipName(p)) return false;
  int32_t dstUtc = stdUtc + 3600;
  if (*p && *p != ',') {
    int32_t dstOffset;
    if (!readTime(p, dstOffset)) return false;
    dstUtc = -dstOffset;
  }
  ICalZoneRule start, end;  // the change into daylight time, and back
  start.timeOfDay = end.timeOfDay = 2 * 3600;
  if (*p == ',') {
    ++p;
    if (!readRule(p, start.month, start.nth, start.weekday, start.timeOfDay) || *p++ != ',' ||
        !readRule(p, end.month, end.nth, end.weekday, end.timeOfDay) || *p)
      return false;
  } else if (*p) {
    return false;
  } else {  // no rules given: the old US ones
    start.month = 3;
    start.nth = 2;
    end.month = 11;
    end.nth = 1;
  }
  zone.daylight = start;
  zone.daylight.present = zone.daylight.repeats = true;
  zone.daylight.offset = dstUtc;
  zone.daylight.offsetFrom = stdUtc;
  zone.daylight.fromYear = 1970;
  zone.standard = end;
  zone.standard.present = zone.standard.repeats = true;
  zone.standard.offset = stdUtc;
  zone.standard.offsetFrom = dstUtc;
  zone.standard.fromYear = 1970;
  return true;
}

}  // namespace stella
