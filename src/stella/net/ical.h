#pragma once
// iCalendar (RFC 5545) reading for a device with no heap and little RAM.
//
// `ICalParser` is a streaming parser in the spirit of `JsonParser`: bytes are
// pushed in as they arrive (any chunk size, a megabyte or a single byte), one
// unfolded line is held at a time, and every finished VEVENT is handed to an
// `ICalSink` and forgotten. Nothing grows with the size of the file; what does
// not fit (an over-long line, a ninth EXDATE) is cut or counted, never an
// allocation. Time zones come from the file's own VTIMEZONE blocks (a standard
// and a daylight rule each, the way Google Calendar and Apple Calendar write
// them), so a "9:00 in Australia/Melbourne" meeting lands on the right instant
// without a tz database. Times without a zone ("floating") and all-day dates
// belong to wherever the device is; the caller resolves those (`ICalResolver`).
//
// Understood: VEVENT with UID, SUMMARY, LOCATION, DTSTART/DTEND (date or
// date-time, UTC, floating or TZID), DURATION, RRULE (DAILY, WEEKLY, MONTHLY,
// YEARLY with INTERVAL, COUNT, UNTIL, BYDAY with ordinals, BYMONTHDAY, BYMONTH,
// WKST), EXDATE, RECURRENCE-ID overrides, STATUS:CANCELLED. Everything else
// (VALARM, DESCRIPTION, ATTENDEE, VTODO, RDATE, BYSETPOS, hourly rules...) is
// skipped; an event whose RRULE cannot be followed is delivered as a single
// occurrence and counted in `Stats::unsupportedRules`.
#include <stddef.h>
#include <stdint.h>

#include "../core/civil.h"

namespace stella {

// One yearly onset (a STANDARD or DAYLIGHT observance of a VTIMEZONE).
struct ICalZoneRule {
  bool present = false;
  bool repeats = false;     // had a yearly RRULE: the switch recurs
  int32_t offset = 0;       // TZOFFSETTO, seconds east of UTC
  int32_t offsetFrom = 0;   // TZOFFSETFROM
  int32_t timeOfDay = 0;    // the local time of the switch, seconds after midnight
  uint8_t month = 1;        // 1..12
  int8_t nth = 0;           // 1..5 or -1 (last) with `weekday`; 0 = the fixed `day`
  int8_t weekday = 0;       // 0 = Sunday
  uint8_t day = 1;
  int16_t fromYear = 0;     // the year of its DTSTART
};

struct ICalZone {
  char tzid[40] = {0};
  ICalZoneRule standard;
  ICalZoneRule daylight;

  bool valid() const { return standard.present || daylight.present; }
  // Seconds east of UTC at a UTC instant.
  int32_t offsetAtUtc(int64_t utc) const;
  // The UTC instant of a wall-clock time here. A time that happens twice (the
  // clocks go back) is the first one; a time that never happens (the clocks go
  // forward) is read with the offset from before the switch.
  int64_t toUtc(int64_t wall) const;
  int64_t toWall(int64_t utc) const { return utc + offsetAtUtc(utc); }
};

// What the caller knows that the file cannot say: the device's own zone.
class ICalResolver {
 public:
  virtual ~ICalResolver() = default;
  // The UTC instant of a wall-clock time on this device (floating times, all-day dates).
  virtual int64_t localToEpoch(int64_t wallSeconds) const = 0;
};

class UtcResolver : public ICalResolver {
 public:
  int64_t localToEpoch(int64_t wallSeconds) const override { return wallSeconds; }
};

enum class ICalFreq : uint8_t { None, Daily, Weekly, Monthly, Yearly };

struct ICalRule {
  ICalFreq freq = ICalFreq::None;
  bool unsupported = false;
  uint8_t wkst = 1;          // week start, 0 = Sunday (RFC default Monday)
  uint16_t interval = 1;
  uint16_t count = 0;        // 0 = no COUNT
  int64_t until = -1;        // wall seconds in the event's own frame; -1 = none
  uint16_t byMonth = 0;      // bit 0 = January
  uint8_t byDay = 0;         // bit 0 = Sunday: every such weekday
  uint8_t ordCount = 0;      // BYDAY with an ordinal: "the first Monday", "the last Friday"
  struct Ordinal {
    int8_t nth;
    uint8_t weekday;
  } ordinal[4] = {};
  uint8_t monthDayCount = 0; // BYMONTHDAY, negative counts from the end of the month
  int8_t monthDay[6] = {};
};

enum class ICalZoneKind : uint8_t { Floating, Utc, Named };

struct ICalEvent {
  static constexpr size_t kMaxExceptions = 8;
  uint32_t uid = 0;          // a hash of the UID: enough to pair overrides with their series
  char summary[40] = {0};
  char location[24] = {0};
  bool allDay = false;
  bool cancelled = false;
  ICalZoneKind zoneKind = ICalZoneKind::Floating;
  int64_t startWall = 0;     // DTSTART read off the clock; all-day: midnight of the date
  int32_t duration = 0;      // seconds (all-day: a whole number of days)
  ICalRule rule;
  bool hasRecurrenceId = false;  // this replaces one occurrence of the series with `uid`
  int64_t recurrenceId = 0;      // wall seconds of the occurrence it replaces
  uint8_t exceptionCount = 0;
  int32_t exception[kMaxExceptions] = {};  // EXDATEs, seconds from startWall
};

class ICalSink {
 public:
  virtual ~ICalSink() = default;
  // `zone` is the event's own VTIMEZONE when zoneKind is Named; it is only
  // valid for the duration of the call (copy it to keep it).
  virtual void onEvent(const ICalEvent &event, const ICalZone *zone) = 0;
};

class ICalParser {
 public:
  static constexpr size_t kLineBytes = 320;   // one unfolded line; the rest of a longer one is dropped
  static constexpr size_t kMaxZones = 4;
  static constexpr int kMaxDepth = 8;

  struct Stats {
    uint32_t events = 0;           // delivered to the sink
    uint32_t skipped = 0;          // no usable DTSTART, or an invalid date
    uint32_t unsupportedRules = 0;
    uint32_t unresolvedZones = 0;  // a TZID the file never defined (read as floating)
    uint32_t cutLines = 0;         // lines longer than kLineBytes (mostly DESCRIPTION)
    uint32_t zonesDropped = 0;     // more than kMaxZones, or unusable
  };

  explicit ICalParser(ICalSink &sink) : sink_(sink) {}
  void reset();
  // Returns false if this is not a calendar; the parser then ignores the rest.
  bool feed(const char *data, size_t size);
  // True if the calendar was closed properly (END:VCALENDAR).
  bool finish();
  const char *error() const { return error_; }
  const Stats &stats() const { return stats_; }

 private:
  enum class Part : uint8_t { Other, Calendar, Event, Zone, Standard, Daylight };
  struct Stamp {  // a parsed DATE or DATE-TIME
    bool have = false;
    bool dateOnly = false;
    bool utc = false;
    int64_t wall = 0;
    char tzid[40] = {0};
  };
  struct Working {
    uint32_t uid = 0;
    char summary[40] = {0};
    char location[24] = {0};
    bool cancelled = false;
    bool bad = false;  // a date that is not a date
    Stamp start, end, recurrenceId;
    bool haveDuration = false;
    int32_t duration = 0;
    ICalRule rule;
    bool haveUntil = false;
    Stamp until;
    uint8_t exceptionCount = 0;
    Stamp exception[ICalEvent::kMaxExceptions];
  };
  struct Observance {
    ICalZoneRule rule;
    bool haveOffset = false;
    int month = 1, day = 1;
  };

  void process();
  void endOfLine(char next);
  void line(char *text);
  void begin(const char *name);
  void end(const char *name);
  void eventProperty(const char *name, const char *params, const char *value);
  void zoneProperty(const char *name, const char *params, const char *value);
  void finishEvent();
  void finishObservance(bool daylight);
  void finishZone();
  bool parseRule(const char *value, ICalRule &rule, Stamp *until, bool &haveUntil);
  const ICalZone *findZone(const char *tzid) const;
  void fail(const char *why);

  ICalSink &sink_;
  char line_[kLineBytes];
  size_t length_ = 0;
  bool cut_ = false;
  bool pendingEol_ = false;  // a line break was seen: continue only if the next byte folds it
  bool inLine_ = false;
  bool started_ = false;
  bool closed_ = false;
  bool failed_ = false;
  Part stack_[kMaxDepth];
  int depth_ = 0;  // may exceed kMaxDepth: deeper parts are all "Other"
  Working work_;
  Observance obs_;
  ICalZone tz_;
  ICalZone zones_[kMaxZones];
  size_t zoneCount_ = 0;
  Stats stats_;
  const char *error_ = nullptr;
};

}  // namespace stella
