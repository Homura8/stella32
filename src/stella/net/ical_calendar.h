#pragma once
// A bounded set of calendar events, filled by an `ICalParser` and asked "what
// happens between these two instants?". It keeps only what can matter for the
// window it was given (a month or two around now): past one-off events are
// dropped as they stream by, a series is kept as one entry and expanded on
// demand, and an occurrence that was moved or cancelled (RECURRENCE-ID,
// EXDATE, STATUS) is left out. Storage is fixed (`kMaxEvents`); what does not
// fit is counted in `dropped()`.
#include <stddef.h>
#include <stdint.h>

#include "ical.h"

namespace stella {

struct ICalOccurrence {
  int64_t start = 0;  // UTC instants; all-day: local midnight to local midnight
  int64_t end = 0;
  bool allDay = false;
  const char *summary = "";   // owned by the calendar: valid until it is changed
  const char *location = "";
};

class ICalCalendar : public ICalSink {
 public:
  static constexpr size_t kMaxEvents = 32;

  // What to keep, as UTC instants. Without a window everything is kept.
  void setWindow(int64_t fromUtc, int64_t toUtc) {
    from_ = fromUtc;
    to_ = toUtc;
    windowed_ = true;
  }
  void clear();

  void onEvent(const ICalEvent &event, const ICalZone *zone) override;

  size_t size() const { return count_; }
  uint32_t dropped() const { return dropped_; }        // did not fit (not outside the window)
  uint32_t outsideWindow() const { return outside_; }  // one-off past or far-future events, series that ended

  // The occurrences that overlap [fromUtc, toUtc), earliest first, at most
  // `max`. `more` (optional) says there were more than fitted. All-day dates
  // and floating times are read in the device's zone by `resolver`.
  size_t occurrences(int64_t fromUtc, int64_t toUtc, const ICalResolver &resolver, ICalOccurrence *out, size_t max,
                     bool *more = nullptr) const;

 private:
  static constexpr uint8_t kNoZone = 0xff;
  struct Stored {
    ICalEvent event;
    uint8_t zone = kNoZone;
  };
  bool overridden(uint32_t uid, int64_t wall) const;
  uint8_t keepZone(const ICalZone *zone);

  Stored events_[kMaxEvents];
  size_t count_ = 0;
  ICalZone zones_[ICalParser::kMaxZones];
  size_t zoneCount_ = 0;
  int64_t from_ = 0, to_ = 0;
  bool windowed_ = false;
  uint32_t dropped_ = 0;
  uint32_t outside_ = 0;
};

}  // namespace stella
