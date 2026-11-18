#pragma once
// The device's own time zone as the thread-safe object the calendar needs: the
// POSIX TZ string the settings keep ("AEST-10AEDT,M10.1.0,M4.1.0/3") read into
// the same rule pair an iCalendar VTIMEZONE gives. libc's mktime() would do the
// same but reads process-wide state the UI thread is using, and the calendar's
// service thread must not touch that.
//
// Understood: "STD offset", "STD offset DST [offset]" with the usual
// ",Mm.w.d[/time],Mm.w.d[/time]" rules (w = 5 means the last week), names in
// letters or in <angle brackets>. Julian-day rules (Jn, n) are not.
#include "ical.h"

namespace stella {

// False if `tz` is empty or in a form not understood (`zone` is then UTC).
bool parsePosixZone(const char *tz, ICalZone &zone);

class PosixZoneResolver : public ICalResolver {
 public:
  // Set the zone; a string that is not understood leaves UTC and returns false.
  bool set(const char *tz) { return parsePosixZone(tz, zone_); }
  int64_t localToEpoch(int64_t wallSeconds) const override { return zone_.toUtc(wallSeconds); }
  int64_t epochToLocal(int64_t epoch) const { return zone_.toWall(epoch); }

 private:
  ICalZone zone_ = utc();
  static ICalZone utc() {
    ICalZone z;
    z.standard.present = true;
    return z;
  }
};

}  // namespace stella
