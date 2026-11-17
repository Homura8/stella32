#include "ical_calendar.h"

#include <string.h>

namespace stella {

namespace {

constexpr int64_t kSlack = 2 * 86400;  // wall clocks differ from UTC by at most a day; be generous
constexpr int kMaxPeriods = 20000;     // a bound on the work for one series, whatever the file says

int monthOf(int64_t day) {
  int y, m, d;
  civil::civilFromDays(day, y, m, d);
  return m;
}

// The days (day numbers) of one month a rule picks, ascending. `defaultDay`
// applies when the rule names no day at all (0: none, the month contributes nothing).
size_t monthDays(const ICalRule &r, int year, int month, int defaultDay, int64_t *out) {
  const int dim = civil::daysInMonth(year, month);
  const int64_t first = civil::daysFromCivil(year, month, 1);
  bool mark[32] = {false};
  const bool hasMonthDay = r.monthDayCount > 0;
  const bool hasWeekday = r.byDay != 0 || r.ordCount > 0;
  bool weekdayMark[32] = {false};
  if (hasWeekday) {
    for (int day = 1; day <= dim; ++day)
      if (r.byDay & (1u << civil::weekday(first + day - 1))) weekdayMark[day] = true;
    for (uint8_t i = 0; i < r.ordCount; ++i) {
      int64_t d;
      if (civil::nthWeekday(year, month, r.ordinal[i].nth, r.ordinal[i].weekday, d)) weekdayMark[d - first + 1] = true;
    }
  }
  if (hasMonthDay) {
    for (uint8_t i = 0; i < r.monthDayCount; ++i) {
      const int md = r.monthDay[i];
      const int day = md > 0 ? md : dim + md + 1;
      if (day >= 1 && day <= dim && (!hasWeekday || weekdayMark[day])) mark[day] = true;
    }
  } else if (hasWeekday) {
    for (int day = 1; day <= dim; ++day) mark[day] = weekdayMark[day];
  } else if (defaultDay >= 1 && defaultDay <= dim) {
    mark[defaultDay] = true;
  }
  size_t n = 0;
  for (int day = 1; day <= dim; ++day)
    if (mark[day]) out[n++] = first + day - 1;
  return n;
}

// Calls fn(wall) for the occurrences of an event in ascending order, from the
// first one, until the series ends or `hiWall` is passed. Occurrences that end
// before `loWall` are counted (COUNT) but not reported. fn returns false to stop.
template <class Fn>
void forEachWall(const ICalEvent &ev, int64_t loWall, int64_t hiWall, Fn fn) {
  const ICalRule &r = ev.rule;
  if (r.freq == ICalFreq::None) {
    if (ev.startWall <= hiWall && ev.startWall + ev.duration >= loWall) fn(ev.startWall);
    return;
  }
  const int64_t d0 = civil::floorDiv(ev.startWall, 86400);
  const int64_t tod = civil::floorMod(ev.startWall, 86400);
  const int64_t loDay = civil::floorDiv(loWall - ev.duration, 86400) - 1;
  uint32_t emitted = 0;
  bool done = false;
  // One candidate day: false once nothing later can matter.
  auto candidate = [&](int64_t day) -> bool {
    if (day < d0) return true;
    const int64_t wall = day * 86400 + tod;
    if (r.until >= 0 && wall > r.until) return false;
    if (wall > hiWall) return false;
    if (r.count && ++emitted > r.count) return false;
    if (wall + ev.duration < loWall) return true;
    for (uint8_t i = 0; i < ev.exceptionCount; ++i)
      if (ev.startWall + ev.exception[i] == wall) return true;
    return fn(wall);
  };
  const bool skip = r.count == 0;  // with COUNT every occurrence must be counted from the start
  const int64_t interval = r.interval ? r.interval : 1;
  int64_t days[32];
  switch (r.freq) {
    case ICalFreq::Daily: {
      int64_t k = skip ? civil::floorDiv(loDay - d0, interval) - 1 : 0;
      if (k < 0) k = 0;
      for (int n = 0; n < kMaxPeriods && !done; ++n, ++k) {
        const int64_t day = d0 + k * interval;
        const bool ok = (!r.byMonth || (r.byMonth & (1u << (monthOf(day) - 1)))) &&
                        (!r.byDay || (r.byDay & (1u << civil::weekday(day))));
        bool monthDayOk = r.monthDayCount == 0;
        if (!monthDayOk) {
          int y, m, d;
          civil::civilFromDays(day, y, m, d);
          const int dim = civil::daysInMonth(y, m);
          for (uint8_t i = 0; i < r.monthDayCount; ++i)
            if (r.monthDay[i] == d || r.monthDay[i] == d - dim - 1) monthDayOk = true;
        }
        if (ok && monthDayOk && !candidate(day)) done = true;
        if (day > hiWall / 86400 + 1) done = true;
      }
      break;
    }
    case ICalFreq::Weekly: {
      const int64_t base = d0 - civil::floorMod(civil::weekday(d0) - r.wkst, 7);
      uint8_t mask = r.byDay;
      for (uint8_t i = 0; i < r.ordCount; ++i) mask = (uint8_t)(mask | (1u << r.ordinal[i].weekday));
      if (!mask) mask = (uint8_t)(1u << civil::weekday(d0));
      int64_t k = skip ? civil::floorDiv(loDay - base, 7 * interval) - 1 : 0;
      if (k < 0) k = 0;
      for (int n = 0; n < kMaxPeriods && !done; ++n, ++k) {
        const int64_t ws = base + 7 * interval * k;
        for (int i = 0; i < 7 && !done; ++i) {
          const int64_t day = ws + i;
          if (!(mask & (1u << civil::weekday(day)))) continue;
          if (r.byMonth && !(r.byMonth & (1u << (monthOf(day) - 1)))) continue;
          if (!candidate(day)) done = true;
        }
        if (ws > hiWall / 86400 + 7) done = true;
      }
      break;
    }
    case ICalFreq::Monthly:
    case ICalFreq::Yearly: {
      int y0, m0, dd0;
      civil::civilFromDays(d0, y0, m0, dd0);
      const bool yearly = r.freq == ICalFreq::Yearly;
      int64_t k = 0;
      if (skip) {
        int yl, ml, dl;
        civil::civilFromDays(loDay, yl, ml, dl);
        k = yearly ? civil::floorDiv(yl - y0, interval) - 1 : civil::floorDiv((int64_t)(yl - y0) * 12 + (ml - m0), interval) - 1;
        if (k < 0) k = 0;
      }
      for (int n = 0; n < kMaxPeriods && !done; ++n, ++k) {
        int year, firstMonth, lastMonth;
        if (yearly) {
          year = (int)(y0 + k * interval);
          firstMonth = 1;
          lastMonth = 12;
        } else {
          const int64_t mi = (int64_t)y0 * 12 + (m0 - 1) + k * interval;
          year = (int)civil::floorDiv(mi, 12);
          firstMonth = lastMonth = (int)civil::floorMod(mi, 12) + 1;
        }
        if (year > 2999) break;
        for (int month = firstMonth; month <= lastMonth && !done; ++month) {
          if (r.byMonth) {
            if (!(r.byMonth & (1u << (month - 1)))) continue;
          } else if (yearly && month != m0 && !(r.monthDayCount || r.byDay || r.ordCount)) {
            continue;
          }
          const size_t count = monthDays(r, year, month, dd0, days);
          for (size_t i = 0; i < count && !done; ++i)
            if (!candidate(days[i])) done = true;
        }
        if (civil::daysFromCivil(year, 1, 1) > hiWall / 86400 + 366) done = true;
      }
      break;
    }
    default: break;
  }
}

}  // namespace

void ICalCalendar::clear() {
  for (size_t i = 0; i < kMaxEvents; ++i) events_[i] = Stored();
  count_ = 0;
  zoneCount_ = 0;
  dropped_ = outside_ = 0;
}

uint8_t ICalCalendar::keepZone(const ICalZone *zone) {
  if (!zone) return kNoZone;
  for (size_t i = 0; i < zoneCount_; ++i)
    if (strcmp(zones_[i].tzid, zone->tzid) == 0) return (uint8_t)i;
  if (zoneCount_ >= ICalParser::kMaxZones) return kNoZone;
  zones_[zoneCount_] = *zone;
  return (uint8_t)zoneCount_++;
}

void ICalCalendar::onEvent(const ICalEvent &event, const ICalZone *zone) {
  if (event.cancelled && !event.hasRecurrenceId) return;  // nothing happens; an override stays to hide its slot
  if (windowed_) {
    const int64_t lo = from_ - kSlack, hi = to_ + kSlack;
    bool relevant;
    if (event.rule.freq != ICalFreq::None) {
      relevant = event.startWall <= hi && (event.rule.until < 0 || event.rule.until + event.duration >= lo);
    } else {
      relevant = event.startWall <= hi && event.startWall + event.duration >= lo;
      if (!relevant && event.hasRecurrenceId) relevant = event.recurrenceId >= lo && event.recurrenceId <= hi;
    }
    if (!relevant) {
      ++outside_;
      return;
    }
  }
  if (count_ >= kMaxEvents) {
    ++dropped_;
    return;
  }
  Stored &s = events_[count_++];
  s.event = event;
  s.zone = keepZone(zone);
  if (event.zoneKind == ICalZoneKind::Named && s.zone == kNoZone) s.event.zoneKind = ICalZoneKind::Floating;
}

bool ICalCalendar::overridden(uint32_t uid, int64_t wall) const {
  for (size_t i = 0; i < count_; ++i) {
    const ICalEvent &e = events_[i].event;
    if (e.hasRecurrenceId && e.uid == uid && e.recurrenceId == wall) return true;
  }
  return false;
}

size_t ICalCalendar::occurrences(int64_t fromUtc, int64_t toUtc, const ICalResolver &resolver, ICalOccurrence *out, size_t max,
                                 bool *more) const {
  size_t n = 0;
  if (more) *more = false;
  if (max == 0) return 0;
  for (size_t i = 0; i < count_; ++i) {
    const Stored &s = events_[i];
    const ICalEvent &ev = s.event;
    if (ev.cancelled) continue;
    const ICalZone *zone = s.zone == kNoZone ? nullptr : &zones_[s.zone];
    forEachWall(ev, fromUtc - kSlack - ev.duration, toUtc + kSlack, [&](int64_t wall) -> bool {
      if (ev.rule.freq != ICalFreq::None && ev.uid != 0 && overridden(ev.uid, wall)) return true;
      ICalOccurrence o;
      if (ev.allDay) {
        o.start = resolver.localToEpoch(wall);
        o.end = resolver.localToEpoch(wall + ev.duration);
      } else {
        o.start = ev.zoneKind == ICalZoneKind::Utc     ? wall
                  : ev.zoneKind == ICalZoneKind::Named ? zone->toUtc(wall)
                                                       : resolver.localToEpoch(wall);
        o.end = o.start + ev.duration;
      }
      const bool overlaps = ev.duration > 0 ? (o.end > fromUtc && o.start < toUtc) : (o.start >= fromUtc && o.start < toUtc);
      if (!overlaps) return true;
      o.allDay = ev.allDay;
      o.summary = ev.summary;
      o.location = ev.location;
      // insert in order of start, keeping the earliest `max`
      size_t at = n;
      while (at > 0 && (out[at - 1].start > o.start || (out[at - 1].start == o.start && out[at - 1].end > o.end))) --at;
      if (n < max) {
        for (size_t j = n; j > at; --j) out[j] = out[j - 1];
        out[at] = o;
        ++n;
      } else {
        if (more) *more = true;
        if (at < max) {
          for (size_t j = max - 1; j > at; --j) out[j] = out[j - 1];
          out[at] = o;
        }
      }
      return true;
    });
  }
  return n;
}

}  // namespace stella
