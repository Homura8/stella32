#include "ical.h"

#include <initializer_list>
#include <string.h>

#include "../core/text.h"

namespace stella {

namespace {

bool eq(const char *a, const char *b) {  // case-insensitive, ASCII
  for (;; ++a, ++b) {
    char x = *a, y = *b;
    if (x >= 'a' && x <= 'z') x = (char)(x - 32);
    if (y >= 'a' && y <= 'z') y = (char)(y - 32);
    if (x != y) return false;
    if (x == 0) return true;
  }
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// n digits starting at p; false if any is not a digit.
bool digits(const char *p, int n, int &out) {
  int v = 0;
  for (int i = 0; i < n; ++i) {
    if (!isDigit(p[i])) return false;
    v = v * 10 + (p[i] - '0');
  }
  out = v;
  return true;
}

// A small non-negative or signed integer; stops at the first non-digit.
bool parseInt(const char *&p, int &out) {
  bool neg = false;
  if (*p == '+' || *p == '-') neg = *p++ == '-';
  if (!isDigit(*p)) return false;
  long v = 0;
  while (isDigit(*p)) {
    v = v * 10 + (*p++ - '0');
    if (v > 100000) v = 100000;
  }
  out = (int)(neg ? -v : v);
  return true;
}

int weekdayCode(const char *p) {  // "SU".."SA" -> 0..6, else -1
  static const char *const kNames[7] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
  for (int i = 0; i < 7; ++i)
    if ((p[0] == kNames[i][0] || p[0] == kNames[i][0] + 32) && (p[1] == kNames[i][1] || p[1] == kNames[i][1] + 32)) return i;
  return -1;
}

// "+1100", "-0930", "+103000" -> seconds east.
bool parseOffset(const char *v, int32_t &out) {
  if (*v != '+' && *v != '-') return false;
  const int sign = *v == '-' ? -1 : 1;
  int h = 0, m = 0, s = 0;
  if (!digits(v + 1, 2, h) || !digits(v + 3, 2, m)) return false;
  if (v[5] && !digits(v + 5, 2, s)) return false;
  out = sign * (h * 3600 + m * 60 + s);
  return true;
}

// P1W, P2DT3H30M, PT45M, -PT15M (a negative duration reads as zero).
bool parseDuration(const char *v, int32_t &out) {
  bool neg = false;
  if (*v == '-' || *v == '+') neg = *v++ == '-';
  if (*v != 'P' && *v != 'p') return false;
  ++v;
  int64_t total = 0;
  bool time = false, any = false;
  while (*v) {
    if (*v == 'T' || *v == 't') {
      time = true;
      ++v;
      continue;
    }
    int n = 0;
    if (!parseInt(v, n) || n < 0) return false;
    const char unit = *v++ & ~0x20;
    switch (unit) {
      case 'W': total += (int64_t)n * 7 * 86400; break;
      case 'D': total += (int64_t)n * 86400; break;
      case 'H': total += (int64_t)n * 3600; break;
      case 'M': total += time ? (int64_t)n * 60 : 0; break;
      case 'S': total += n; break;
      default: return false;
    }
    any = true;
  }
  if (!any) return false;
  if (neg || total < 0) total = 0;
  if (total > 0x7fffffff) total = 0x7fffffff;
  out = (int32_t)total;
  return true;
}

// Copy a TEXT value: \n -> space, \, \; \\ -> the character, cut at a UTF-8 boundary.
void unescapeText(const char *src, char *dst, size_t capacity) {
  char tmp[112];
  size_t n = 0;
  for (; *src && n + 1 < sizeof(tmp); ++src) {
    if (*src == '\\' && src[1]) {
      ++src;
      tmp[n++] = (*src == 'n' || *src == 'N') ? ' ' : *src;
    } else {
      tmp[n++] = *src;
    }
  }
  tmp[n] = '\0';
  copyText(dst, capacity, tmp);
}

// A parameter value ("TZID=Australia/Melbourne", quoted or not) from the part
// of a content line between the name and the colon.
bool findParam(const char *params, const char *key, char *out, size_t capacity) {
  const size_t klen = strlen(key);
  const char *p = params;
  while (*p) {
    const char *eqSign = p;
    while (*eqSign && *eqSign != '=' && *eqSign != ';') ++eqSign;
    const bool match = *eqSign == '=' && (size_t)(eqSign - p) == klen;
    bool same = match;
    for (size_t i = 0; same && i < klen; ++i) {
      char a = p[i], b = key[i];
      if (a >= 'a' && a <= 'z') a = (char)(a - 32);
      same = a == b;
    }
    const char *v = *eqSign == '=' ? eqSign + 1 : eqSign;
    bool quoted = false;
    const char *e = v;
    while (*e && (quoted || *e != ';')) {
      if (*e == '"') quoted = !quoted;
      ++e;
    }
    if (same) {
      size_t n = 0;
      for (const char *c = v; c < e && n + 1 < capacity; ++c)
        if (*c != '"') out[n++] = *c;
      out[n] = '\0';
      return true;
    }
    p = *e ? e + 1 : e;
  }
  return false;
}

}  // namespace

// --- Zones -------------------------------------------------------------

namespace {

int64_t onset(const ICalZoneRule &r, int year) {
  int64_t day = 0;
  if (r.nth != 0) {
    if (!civil::nthWeekday(year, r.month, r.nth, r.weekday, day)) civil::nthWeekday(year, r.month, -1, r.weekday, day);
  } else {
    const int dim = civil::daysInMonth(year, r.month);
    day = civil::daysFromCivil(year, r.month, r.day < dim ? r.day : dim);
  }
  return day * 86400 + r.timeOfDay - r.offsetFrom;  // the switch happens on the clock of the old offset
}

bool alternates(const ICalZone &z) { return z.standard.present && z.daylight.present && z.standard.repeats && z.daylight.repeats; }

}  // namespace

int32_t ICalZone::offsetAtUtc(int64_t utc) const {
  if (!alternates(*this)) {
    if (standard.present && daylight.present) return (daylight.fromYear > standard.fromYear ? daylight : standard).offset;
    return standard.present ? standard.offset : daylight.present ? daylight.offset : 0;
  }
  int y, m, d;
  civil::civilFromDays(civil::floorDiv(utc + standard.offset, 86400), y, m, d);
  const int64_t toDst = onset(daylight, y), toStd = onset(standard, y);
  const bool inDst = toDst < toStd ? (utc >= toDst && utc < toStd) : (utc >= toDst || utc < toStd);
  return inDst ? daylight.offset : standard.offset;
}

int64_t ICalZone::toUtc(int64_t wall) const {
  if (alternates(*this)) {
    for (const int32_t o : {daylight.offset, standard.offset}) {
      const int64_t e = wall - o;
      if (offsetAtUtc(e) == o) return e;
    }
    return wall - standard.offset;
  }
  return wall - offsetAtUtc(wall - (standard.present ? standard.offset : daylight.offset));
}

// --- Parser ------------------------------------------------------------

void ICalParser::reset() {
  length_ = 0;
  cut_ = pendingEol_ = inLine_ = started_ = closed_ = failed_ = false;
  depth_ = 0;
  work_ = Working();
  obs_ = Observance();
  tz_ = ICalZone();
  for (size_t i = 0; i < kMaxZones; ++i) zones_[i] = ICalZone();
  zoneCount_ = 0;
  stats_ = Stats();
  error_ = nullptr;
}

void ICalParser::fail(const char *why) {
  failed_ = true;
  error_ = why;
}

bool ICalParser::feed(const char *data, size_t size) {
  for (size_t i = 0; i < size && !failed_; ++i) {
    const char c = data[i];
    if (c == '\r') continue;
    if (c == '\n') {
      if (pendingEol_) process();  // an empty line
      else if (inLine_) pendingEol_ = true;
      continue;
    }
    if (pendingEol_) {
      if (c == ' ' || c == '\t') {  // a folded line continues: drop the break and the one space
        pendingEol_ = false;
        continue;
      }
      process();
    }
    inLine_ = true;
    if (length_ + 1 < kLineBytes) line_[length_++] = c;
    else cut_ = true;
  }
  return !failed_;
}

bool ICalParser::finish() {
  if (inLine_ && !failed_) process();
  if (!failed_ && !closed_) error_ = started_ ? "The calendar ends early" : "Not an iCalendar file";
  return !failed_ && closed_;
}

void ICalParser::process() {
  line_[length_] = '\0';
  const bool had = length_ > 0;
  if (cut_) ++stats_.cutLines;
  length_ = 0;
  cut_ = inLine_ = pendingEol_ = false;
  if (had) line(line_);
}

void ICalParser::line(char *text) {
  if (!started_ && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text += 3;
  // name [;params] : value, where a colon inside a quoted parameter does not count
  char *p = text;
  while (*p && *p != ';' && *p != ':') ++p;
  char *name = text;
  char *params = nullptr;
  char *value = nullptr;
  if (*p == ';') {
    *p++ = '\0';
    params = p;
    bool quoted = false;
    while (*p && (quoted || *p != ':')) {
      if (*p == '"') quoted = !quoted;
      ++p;
    }
    if (*p != ':') return;
    *p++ = '\0';
    value = p;
  } else if (*p == ':') {
    *p++ = '\0';
    value = p;
  } else {
    return;  // not a content line
  }
  if (!started_) {
    if (eq(name, "BEGIN") && eq(value, "VCALENDAR")) started_ = true;
    else return fail("Not an iCalendar file");
  }
  if (eq(name, "BEGIN")) return begin(value);
  if (eq(name, "END")) return end(value);
  if (depth_ < 1 || depth_ > kMaxDepth) return;
  switch (stack_[depth_ - 1]) {
    case Part::Event: eventProperty(name, params ? params : "", value); break;
    case Part::Zone:
    case Part::Standard:
    case Part::Daylight: zoneProperty(name, params ? params : "", value); break;
    default: break;
  }
}

void ICalParser::begin(const char *name) {
  Part part = Part::Other;
  const Part parent = depth_ >= 1 && depth_ <= kMaxDepth ? stack_[depth_ - 1] : Part::Other;
  if (eq(name, "VCALENDAR")) part = Part::Calendar;
  else if (eq(name, "VEVENT") && parent == Part::Calendar) {
    part = Part::Event;
    work_ = Working();
  } else if (eq(name, "VTIMEZONE") && parent == Part::Calendar) {
    part = Part::Zone;
    tz_ = ICalZone();
  } else if ((eq(name, "STANDARD") || eq(name, "DAYLIGHT")) && parent == Part::Zone) {
    part = eq(name, "STANDARD") ? Part::Standard : Part::Daylight;
    obs_ = Observance();
  }
  if (depth_ < kMaxDepth) stack_[depth_] = part;
  ++depth_;
}

void ICalParser::end(const char *) {
  if (depth_ < 1) return;
  --depth_;
  if (depth_ >= kMaxDepth) return;
  switch (stack_[depth_]) {
    case Part::Event: finishEvent(); break;
    case Part::Standard: finishObservance(false); break;
    case Part::Daylight: finishObservance(true); break;
    case Part::Zone: finishZone(); break;
    case Part::Calendar: closed_ = true; break;
    default: break;
  }
}

namespace {

// DATE (20260312) or DATE-TIME (20260312T093000, ...Z); the VALUE parameter and
// a TZID come from the property's parameters.
struct StampText {
  bool ok = false;
  bool dateOnly = false;
  bool utc = false;
  int64_t wall = 0;
};

StampText readStamp(const char *v, bool valueIsDate) {
  StampText s;
  int y, m, d, hh = 0, mm = 0, ss = 0;
  if (!digits(v, 4, y) || !digits(v + 4, 2, m) || !digits(v + 6, 2, d)) return s;
  if (m < 1 || m > 12 || d < 1 || d > civil::daysInMonth(y, m) || y < 1970 || y > 2999) return s;
  if (v[8] == 'T' || v[8] == 't') {
    if (!digits(v + 9, 2, hh) || !digits(v + 11, 2, mm) || !digits(v + 13, 2, ss)) return s;
    if (hh > 23 || mm > 59 || ss > 60) return s;
    if (ss == 60) ss = 59;
    s.utc = v[15] == 'Z' || v[15] == 'z';
  } else if (v[8] != '\0') {
    return s;
  } else {
    s.dateOnly = true;
  }
  if (valueIsDate) s.dateOnly = true;
  s.wall = civil::wallSeconds(y, m, d, hh, mm, ss);
  s.ok = true;
  return s;
}

}  // namespace

void ICalParser::eventProperty(const char *name, const char *params, const char *value) {
  Working &w = work_;
  char tmp[16];
  auto stamp = [&](Stamp &out, const char *text) {
    const bool valueDate = findParam(params, "VALUE", tmp, sizeof(tmp)) && eq(tmp, "DATE");
    const StampText s = readStamp(text, valueDate);
    if (!s.ok) {
      w.bad = true;
      return false;
    }
    out.have = true;
    out.dateOnly = s.dateOnly;
    out.utc = s.utc;
    out.wall = s.wall;
    out.tzid[0] = '\0';
    findParam(params, "TZID", out.tzid, sizeof(out.tzid));
    return true;
  };
  if (eq(name, "UID")) w.uid = hash32(value);
  else if (eq(name, "SUMMARY")) unescapeText(value, w.summary, sizeof(w.summary));
  else if (eq(name, "LOCATION")) unescapeText(value, w.location, sizeof(w.location));
  else if (eq(name, "DTSTART")) stamp(w.start, value);
  else if (eq(name, "DTEND")) stamp(w.end, value);
  else if (eq(name, "RECURRENCE-ID")) stamp(w.recurrenceId, value);
  else if (eq(name, "DURATION")) w.haveDuration = parseDuration(value, w.duration);
  else if (eq(name, "STATUS")) w.cancelled = eq(value, "CANCELLED");
  else if (eq(name, "RRULE")) {
    if (!parseRule(value, w.rule, &w.until, w.haveUntil)) w.rule.unsupported = true;
  } else if (eq(name, "EXDATE")) {
    // several dates on one line, and several lines; all read with this line's parameters
    const bool valueDate = findParam(params, "VALUE", tmp, sizeof(tmp)) && eq(tmp, "DATE");
    char zone[40] = {0};
    findParam(params, "TZID", zone, sizeof(zone));
    for (const char *p = value; *p;) {
      char item[24];
      size_t n = 0;
      while (*p && *p != ',') {
        if (n + 1 < sizeof(item)) item[n++] = *p;
        ++p;
      }
      item[n] = '\0';
      if (*p == ',') ++p;
      const StampText s = readStamp(item, valueDate);
      if (!s.ok || w.exceptionCount >= ICalEvent::kMaxExceptions) continue;
      Stamp &e = w.exception[w.exceptionCount++];
      e.have = true;
      e.dateOnly = s.dateOnly;
      e.utc = s.utc;
      e.wall = s.wall;
      copyText(e.tzid, sizeof(e.tzid), zone);
    }
  }
}

bool ICalParser::parseRule(const char *value, ICalRule &rule, Stamp *until, bool &haveUntil) {
  rule = ICalRule();
  bool ok = true;
  char buf[200];
  copyText(buf, sizeof(buf), value);
  for (char *part = buf; *part;) {
    char *next = strchr(part, ';');
    if (next) *next++ = '\0';
    char *val = strchr(part, '=');
    if (val) {
      *val++ = '\0';
      if (eq(part, "FREQ")) {
        if (eq(val, "DAILY")) rule.freq = ICalFreq::Daily;
        else if (eq(val, "WEEKLY")) rule.freq = ICalFreq::Weekly;
        else if (eq(val, "MONTHLY")) rule.freq = ICalFreq::Monthly;
        else if (eq(val, "YEARLY")) rule.freq = ICalFreq::Yearly;
        else ok = false;  // hourly, by the minute...
      } else if (eq(part, "INTERVAL")) {
        const char *p = val;
        int n = 0;
        if (parseInt(p, n) && n >= 1) rule.interval = (uint16_t)(n > 999 ? 999 : n);
        else ok = false;
      } else if (eq(part, "COUNT")) {
        const char *p = val;
        int n = 0;
        if (parseInt(p, n) && n >= 1) rule.count = (uint16_t)(n > 65535 ? 65535 : n);
        else ok = false;
      } else if (eq(part, "UNTIL") && until) {
        const StampText s = readStamp(val, false);
        if (s.ok) {
          haveUntil = true;
          until->have = true;
          until->dateOnly = s.dateOnly;
          until->utc = s.utc;
          until->wall = s.wall;
        } else {
          ok = false;
        }
      } else if (eq(part, "BYDAY")) {
        for (const char *p = val; *p;) {
          int nth = 0;
          const bool hasNth = (*p == '+' || *p == '-' || isDigit(*p)) && parseInt(p, nth);
          const int wd = weekdayCode(p);
          if (wd < 0) {
            ok = false;
            break;
          }
          p += 2;
          if (hasNth) {
            if (rule.ordCount < 4 && nth != 0 && nth >= -5 && nth <= 5) {
              rule.ordinal[rule.ordCount].nth = (int8_t)nth;
              rule.ordinal[rule.ordCount].weekday = (uint8_t)wd;
              ++rule.ordCount;
            } else {
              ok = false;
            }
          } else {
            rule.byDay = (uint8_t)(rule.byDay | (1u << wd));
          }
          if (*p == ',') ++p;
          else if (*p) {
            ok = false;
            break;
          }
        }
      } else if (eq(part, "BYMONTHDAY")) {
        for (const char *p = val; *p;) {
          int n = 0;
          if (!parseInt(p, n) || n == 0 || n < -31 || n > 31 || rule.monthDayCount >= 6) {
            ok = false;
            break;
          }
          rule.monthDay[rule.monthDayCount++] = (int8_t)n;
          if (*p == ',') ++p;
          else if (*p) {
            ok = false;
            break;
          }
        }
      } else if (eq(part, "BYMONTH")) {
        for (const char *p = val; *p;) {
          int n = 0;
          if (!parseInt(p, n) || n < 1 || n > 12) {
            ok = false;  // also the "5L" leap months of other calendars
            break;
          }
          rule.byMonth = (uint16_t)(rule.byMonth | (1u << (n - 1)));
          if (*p == ',') ++p;
          else if (*p) {
            ok = false;
            break;
          }
        }
      } else if (eq(part, "WKST")) {
        const int wd = weekdayCode(val);
        if (wd >= 0) rule.wkst = (uint8_t)wd;
      } else if (eq(part, "BYSETPOS") || eq(part, "BYHOUR") || eq(part, "BYMINUTE") || eq(part, "BYSECOND") ||
                 eq(part, "BYWEEKNO") || eq(part, "BYYEARDAY") || eq(part, "RSCALE") || eq(part, "SKIP")) {
        ok = false;
      }
    }
    part = next ? next : part + strlen(part);
  }
  if (rule.freq == ICalFreq::None) ok = false;
  if (rule.freq == ICalFreq::Yearly && (rule.byDay || rule.ordCount) && rule.byMonth == 0) ok = false;  // "the n-th Monday of the year"
  if (!ok) rule.unsupported = true;
  return ok;
}

const ICalZone *ICalParser::findZone(const char *tzid) const {
  if (!tzid[0]) return nullptr;
  for (size_t i = 0; i < zoneCount_; ++i)
    if (strcmp(zones_[i].tzid, tzid) == 0) return &zones_[i];
  return nullptr;
}

void ICalParser::finishEvent() {
  Working &w = work_;
  if (!w.start.have || w.bad) {
    ++stats_.skipped;
    return;
  }
  ICalEvent ev;
  ev.uid = w.uid;
  copyText(ev.summary, sizeof(ev.summary), w.summary);
  copyText(ev.location, sizeof(ev.location), w.location);
  ev.cancelled = w.cancelled;
  ev.allDay = w.start.dateOnly;
  ev.startWall = w.start.wall;
  const ICalZone *zone = nullptr;
  if (ev.allDay) {
    ev.zoneKind = ICalZoneKind::Floating;
  } else if (w.start.utc) {
    ev.zoneKind = ICalZoneKind::Utc;
  } else if (w.start.tzid[0]) {
    zone = findZone(w.start.tzid);
    if (zone) {
      ev.zoneKind = ICalZoneKind::Named;
    } else {
      ++stats_.unresolvedZones;
      ev.zoneKind = ICalZoneKind::Floating;
    }
  }
  // The length: from DTEND (as instants, so an event that crosses a clock change keeps its true length)
  // or DURATION, else a day for a date and nothing for a time.
  auto instant = [&](const Stamp &s) -> int64_t {
    if (s.utc) return s.wall;
    const ICalZone *z = s.tzid[0] ? findZone(s.tzid) : nullptr;
    return z ? z->toUtc(s.wall) : s.wall;
  };
  int64_t length = ev.allDay ? 86400 : 0;
  if (w.end.have) {
    const int64_t d = ev.allDay ? w.end.wall - w.start.wall : instant(w.end) - (zone ? zone->toUtc(w.start.wall) : w.start.wall);
    length = d < 0 ? 0 : d;
  } else if (w.haveDuration) {
    length = w.duration;
  }
  if (ev.allDay) length = length < 86400 ? 86400 : (length + 86399) / 86400 * 86400;
  ev.duration = (int32_t)(length > 0x7fffffff ? 0x7fffffff : length);
  // The rule.
  ev.rule = w.rule;
  if (w.haveUntil) {
    const Stamp &u = w.until;
    if (u.dateOnly) ev.rule.until = u.wall + 86399;
    else if (u.utc && zone) ev.rule.until = zone->toWall(u.wall);
    else ev.rule.until = u.wall;
  }
  if (ev.rule.unsupported) {
    ++stats_.unsupportedRules;
    ev.rule.freq = ICalFreq::None;  // delivered as the one occurrence it is known to have
  }
  // Exceptions and replaced occurrences, in the event's own wall clock.
  auto wallOf = [&](const Stamp &s) -> int64_t {
    int64_t wall = s.wall;
    if (s.dateOnly && !ev.allDay) wall += civil::floorMod(ev.startWall, 86400);
    else if (s.utc && zone) wall = zone->toWall(s.wall);
    return wall;
  };
  for (uint8_t i = 0; i < w.exceptionCount; ++i) {
    const int64_t off = wallOf(w.exception[i]) - ev.startWall;
    if (off >= -0x7fffffffLL && off <= 0x7fffffffLL) ev.exception[ev.exceptionCount++] = (int32_t)off;
  }
  if (w.recurrenceId.have) {
    ev.hasRecurrenceId = true;
    ev.recurrenceId = wallOf(w.recurrenceId);
  }
  ++stats_.events;
  sink_.onEvent(ev, zone);
}

void ICalParser::zoneProperty(const char *name, const char *params, const char *value) {
  (void)params;
  const Part part = stack_[depth_ - 1];
  if (part == Part::Zone) {
    if (eq(name, "TZID")) {
      const char *v = value;
      if (*v == '/') {  // "/citadel.org/20190914_1/Europe/Paris": the zone is the tail
        int slashes = 0;
        for (const char *p = v; *p; ++p)
          if (*p == '/' && ++slashes == 3) {
            v = p + 1;
            break;
          }
      }
      copyText(tz_.tzid, sizeof(tz_.tzid), v);
    }
    return;
  }
  if (eq(name, "DTSTART")) {
    const StampText s = readStamp(value, false);
    if (!s.ok) return;
    int y, m, d;
    civil::civilFromDays(civil::floorDiv(s.wall, 86400), y, m, d);
    obs_.rule.fromYear = (int16_t)y;
    obs_.rule.timeOfDay = (int32_t)civil::floorMod(s.wall, 86400);
    obs_.month = m;
    obs_.day = d;
  } else if (eq(name, "TZOFFSETFROM")) {
    parseOffset(value, obs_.rule.offsetFrom);
  } else if (eq(name, "TZOFFSETTO")) {
    obs_.haveOffset = parseOffset(value, obs_.rule.offset);
  } else if (eq(name, "RRULE")) {
    ICalRule r;
    Stamp u;
    bool haveUntil = false;
    if (parseRule(value, r, &u, haveUntil) && r.freq == ICalFreq::Yearly) {
      obs_.rule.repeats = true;
      for (int m = 0; m < 12; ++m)
        if (r.byMonth & (1u << m)) {
          obs_.month = m + 1;
          break;
        }
      if (r.ordCount) {
        obs_.rule.nth = r.ordinal[0].nth;
        obs_.rule.weekday = (int8_t)r.ordinal[0].weekday;
      } else if (r.monthDayCount && r.monthDay[0] > 0) {
        obs_.day = r.monthDay[0];
      }
    }
  }
}

void ICalParser::finishObservance(bool daylight) {
  if (!obs_.haveOffset) return;
  ICalZoneRule r = obs_.rule;
  r.present = true;
  r.month = (uint8_t)obs_.month;
  r.day = (uint8_t)obs_.day;
  ICalZoneRule &slot = daylight ? tz_.daylight : tz_.standard;
  // Files that list a zone's whole history repeat the observance: the newest one wins.
  if (!slot.present || (r.repeats && !slot.repeats) || (r.repeats == slot.repeats && r.fromYear >= slot.fromYear)) slot = r;
}

void ICalParser::finishZone() {
  if (!tz_.tzid[0] || !tz_.valid()) {
    ++stats_.zonesDropped;
    return;
  }
  for (size_t i = 0; i < zoneCount_; ++i)
    if (strcmp(zones_[i].tzid, tz_.tzid) == 0) {
      zones_[i] = tz_;
      return;
    }
  if (zoneCount_ < kMaxZones) zones_[zoneCount_++] = tz_;
  else ++stats_.zonesDropped;
}

}  // namespace stella
