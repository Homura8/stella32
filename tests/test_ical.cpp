// iCalendar reading: a realistic calendar export (time zones, repeats,
// overrides, folded lines, UTF-8), checked against an independent reference
// (python-dateutil + zoneinfo), fed in awkward chunk sizes, in bulk, and
// truncated or mangled in every way the network might.
#include <string.h>

#include <string>
#include <vector>

#include "stella/net/ical.h"
#include "stella/net/ical_calendar.h"
#include "test.h"

using namespace stella;

namespace {

// Australia/Melbourne as the device's own zone (for floating times and all-day dates).
struct MelbourneDevice : ICalResolver {
  ICalZone zone;
  MelbourneDevice() {
    zone.standard = {true, true, 36000, 39600, 3 * 3600, 4, 1, 0, 1, 1970};
    zone.daylight = {true, true, 39600, 36000, 2 * 3600, 10, 1, 0, 1, 1970};
  }
  int64_t localToEpoch(int64_t wall) const override { return zone.toUtc(wall); }
};

const char *kCalendar =
    R"ICS(BEGIN:VCALENDAR
PRODID:-//Google Inc//Google Calendar 70.9054//EN
VERSION:2.0
CALSCALE:GREGORIAN
METHOD:PUBLISH
X-WR-CALNAME:Home
X-WR-TIMEZONE:Australia/Melbourne
BEGIN:VTIMEZONE
TZID:Australia/Melbourne
X-LIC-LOCATION:Australia/Melbourne
BEGIN:DAYLIGHT
TZOFFSETFROM:+1000
TZOFFSETTO:+1100
TZNAME:AEDT
DTSTART:19701004T020000
RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=1SU
END:DAYLIGHT
BEGIN:STANDARD
TZOFFSETFROM:+1100
TZOFFSETTO:+1000
TZNAME:AEST
DTSTART:19700405T030000
RRULE:FREQ=YEARLY;BYMONTH=4;BYDAY=1SU
END:STANDARD
END:VTIMEZONE
BEGIN:VTIMEZONE
TZID:America/New_York
X-LIC-LOCATION:America/New_York
BEGIN:DAYLIGHT
TZOFFSETFROM:-0500
TZOFFSETTO:-0400
TZNAME:EDT
DTSTART:19700308T020000
RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=2SU
END:DAYLIGHT
BEGIN:STANDARD
TZOFFSETFROM:-0400
TZOFFSETTO:-0500
TZNAME:EST
DTSTART:19701101T020000
RRULE:FREQ=YEARLY;BYMONTH=11;BYDAY=1SU
END:STANDARD
END:VTIMEZONE
BEGIN:VEVENT
UID:pilates@example.test
SUMMARY:Pilates
DTSTART;TZID=Australia/Melbourne:20260302T180000
DTEND;TZID=Australia/Melbourne:20260302T190000
RRULE:FREQ=WEEKLY;BYDAY=MO,TH
ATTENDEE;CN="Zoe: Studio";ROLE=REQ-PARTICIPANT:mailto:studio@example.test
DESCRIPTION:Bring a mat and water. SUMMARY:Not this. DTSTART:20990101T000000Z
BEGIN:VALARM
ACTION:DISPLAY
DESCRIPTION:Reminder
TRIGGER:-PT10M
END:VALARM
END:VEVENT
BEGIN:VEVENT
UID:birthday@example.test
SUMMARY:Mum's birthday
DTSTART;VALUE=DATE:20100412
DTEND;VALUE=DATE:20100413
RRULE:FREQ=YEARLY
TRANSP:TRANSPARENT
END:VEVENT
BEGIN:VEVENT
UID:standup@example.test
SUMMARY:Standup
DTSTART:20260301T230000Z
DTEND:20260301T231500Z
RRULE:FREQ=DAILY;COUNT=10
EXDATE:20260303T230000Z
END:VEVENT
BEGIN:VEVENT
UID:board@example.test
SUMMARY:Board call
DTSTART;TZID=America/New_York:20260105T100000
DURATION:PT1H30M
RRULE:FREQ=MONTHLY;BYDAY=1MO
END:VEVENT
BEGIN:VEVENT
UID:drinks@example.test
SUMMARY:Drinks
DTSTART:20260130T173000
DTEND:20260130T193000
RRULE:FREQ=MONTHLY;BYDAY=-1FR
END:VEVENT
BEGIN:VEVENT
UID:team@example.test
SUMMARY:Team meeting
DTSTART;TZID=Australia/Melbourne:20260302T090000
DTEND;TZID=Australia/Melbourne:20260302T100000
RRULE:FREQ=WEEKLY;BYDAY=MO
EXDATE;TZID=Australia/Melbourne:20260309T090000
END:VEVENT
BEGIN:VEVENT
UID:team@example.test
RECURRENCE-ID;TZID=Australia/Melbourne:20260316T090000
SUMMARY:Team meeting (moved)
DTSTART;TZID=Australia/Melbourne:20260310T140000
DTEND;TZID=Australia/Melbourne:20260310T150000
END:VEVENT
BEGIN:VEVENT
UID:team@example.test
RECURRENCE-ID;TZID=Australia/Melbourne:20260323T090000
SUMMARY:Team meeting
STATUS:CANCELLED
DTSTART;TZID=Australia/Melbourne:20260323T090000
DTEND;TZID=Australia/Melbourne:20260323T100000
END:VEVENT
BEGIN:VEVENT
UID:cafe@example.test
DTSTART:20260312T033000Z
DTEND:20260312T043000Z
SUMMARY:Caf)ICS"
    "\xc3\n \xa9 \xe2\x98\x95\\, with Zo\xc3\n \xab\\; ro\n om 4"
    R"ICS(
END:VEVENT
BEGIN:VEVENT
UID:payday@example.test
SUMMARY:Payday
DTSTART;TZID=Australia/Melbourne:20260306T080000
RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=FR;WKST=SU
END:VEVENT
BEGIN:VEVENT
UID:month31@example.test
SUMMARY:Month 31
DTSTART;TZID=Australia/Melbourne:20260131T120000
DURATION:PT30M
RRULE:FREQ=MONTHLY;BYMONTHDAY=31
END:VEVENT
BEGIN:VEVENT
UID:rent@example.test
SUMMARY:Rent
DTSTART;TZID=Australia/Melbourne:20260131T090000
RRULE:FREQ=MONTHLY;BYMONTHDAY=-1
END:VEVENT
BEGIN:VEVENT
UID:trip@example.test
SUMMARY:Canberra trip
DTSTART;VALUE=DATE:20260320
DTEND;VALUE=DATE:20260324
END:VEVENT
BEGIN:VEVENT
UID:swim@example.test
SUMMARY:Swim
DTSTART;TZID=Australia/Melbourne:20260303T070000
DURATION:PT45M
RRULE:FREQ=WEEKLY;BYDAY=TU;UNTIL=20260413T210000Z
END:VEVENT
BEGIN:VEVENT
UID:hourly@example.test
SUMMARY:Hourly thing
DTSTART:20260304T100000Z
RRULE:FREQ=HOURLY;COUNT=5
END:VEVENT
BEGIN:VEVENT
UID:quarterly@example.test
SUMMARY:Quarterly
DTSTART;TZID=Australia/Melbourne:20260315T100000
DTEND;TZID=Australia/Melbourne:20260315T110000
RRULE:FREQ=YEARLY;BYMONTH=3,6,9,12;BYMONTHDAY=15
END:VEVENT
BEGIN:VEVENT
UID:old@example.test
SUMMARY:Long ago
DTSTART:20190501T100000Z
DTEND:20190501T110000Z
END:VEVENT
BEGIN:VEVENT
UID:broken@example.test
SUMMARY:Not a date
DTSTART:20260230T100000
END:VEVENT
END:VCALENDAR
)ICS";

constexpr int64_t kFrom = 1772323200;  // 2026-03-01T00:00:00Z
constexpr int64_t kTo = 1780272000;    // 2026-06-01T00:00:00Z

struct Expect {
  const char *summary;
  int64_t start, end;
  bool allDay;
};

// Generated with python-dateutil's rrule and zoneinfo from the same definitions.
const Expect kExpected[] = {
  {"Team meeting", 1772402400LL, 1772406000LL, false},
  {"Standup", 1772406000LL, 1772406900LL, false},
  {"Pilates", 1772434800LL, 1772438400LL, false},
  {"Board call", 1772463600LL, 1772469000LL, false},
  {"Swim", 1772481600LL, 1772484300LL, false},
  {"Standup", 1772492400LL, 1772493300LL, false},
  {"Hourly thing", 1772618400LL, 1772618400LL, false},
  {"Standup", 1772665200LL, 1772666100LL, false},
  {"Pilates", 1772694000LL, 1772697600LL, false},
  {"Payday", 1772744400LL, 1772744400LL, false},
  {"Standup", 1772751600LL, 1772752500LL, false},
  {"Standup", 1772838000LL, 1772838900LL, false},
  {"Standup", 1772924400LL, 1772925300LL, false},
  {"Standup", 1773010800LL, 1773011700LL, false},
  {"Pilates", 1773039600LL, 1773043200LL, false},
  {"Swim", 1773086400LL, 1773089100LL, false},
  {"Standup", 1773097200LL, 1773098100LL, false},
  {"Team meeting (moved)", 1773111600LL, 1773115200LL, false},
  {"Standup", 1773183600LL, 1773184500LL, false},
  {"Café ☕, with Zoë; room 4", 1773286200LL, 1773289800LL, false},
  {"Pilates", 1773298800LL, 1773302400LL, false},
  {"Quarterly", 1773529200LL, 1773532800LL, false},
  {"Pilates", 1773644400LL, 1773648000LL, false},
  {"Swim", 1773691200LL, 1773693900LL, false},
  {"Pilates", 1773903600LL, 1773907200LL, false},
  {"Canberra trip", 1773925200LL, 1774270800LL, true},
  {"Payday", 1773954000LL, 1773954000LL, false},
  {"Pilates", 1774249200LL, 1774252800LL, false},
  {"Swim", 1774296000LL, 1774298700LL, false},
  {"Pilates", 1774508400LL, 1774512000LL, false},
  {"Drinks", 1774593000LL, 1774600200LL, false},
  {"Team meeting", 1774821600LL, 1774825200LL, false},
  {"Pilates", 1774854000LL, 1774857600LL, false},
  {"Swim", 1774900800LL, 1774903500LL, false},
  {"Rent", 1774908000LL, 1774908000LL, false},
  {"Month 31", 1774918800LL, 1774920600LL, false},
  {"Pilates", 1775113200LL, 1775116800LL, false},
  {"Payday", 1775163600LL, 1775163600LL, false},
  {"Team meeting", 1775430000LL, 1775433600LL, false},
  {"Pilates", 1775462400LL, 1775466000LL, false},
  {"Board call", 1775484000LL, 1775489400LL, false},
  {"Swim", 1775509200LL, 1775511900LL, false},
  {"Pilates", 1775721600LL, 1775725200LL, false},
  {"Mum's birthday", 1775916000LL, 1776002400LL, true},
  {"Team meeting", 1776034800LL, 1776038400LL, false},
  {"Pilates", 1776067200LL, 1776070800LL, false},
  {"Swim", 1776114000LL, 1776116700LL, false},
  {"Pilates", 1776326400LL, 1776330000LL, false},
  {"Payday", 1776376800LL, 1776376800LL, false},
  {"Team meeting", 1776639600LL, 1776643200LL, false},
  {"Pilates", 1776672000LL, 1776675600LL, false},
  {"Pilates", 1776931200LL, 1776934800LL, false},
  {"Drinks", 1777015800LL, 1777023000LL, false},
  {"Team meeting", 1777244400LL, 1777248000LL, false},
  {"Pilates", 1777276800LL, 1777280400LL, false},
  {"Rent", 1777503600LL, 1777503600LL, false},
  {"Pilates", 1777536000LL, 1777539600LL, false},
  {"Payday", 1777586400LL, 1777586400LL, false},
  {"Team meeting", 1777849200LL, 1777852800LL, false},
  {"Pilates", 1777881600LL, 1777885200LL, false},
  {"Board call", 1777903200LL, 1777908600LL, false},
  {"Pilates", 1778140800LL, 1778144400LL, false},
  {"Team meeting", 1778454000LL, 1778457600LL, false},
  {"Pilates", 1778486400LL, 1778490000LL, false},
  {"Pilates", 1778745600LL, 1778749200LL, false},
  {"Payday", 1778796000LL, 1778796000LL, false},
  {"Team meeting", 1779058800LL, 1779062400LL, false},
  {"Pilates", 1779091200LL, 1779094800LL, false},
  {"Pilates", 1779350400LL, 1779354000LL, false},
  {"Team meeting", 1779663600LL, 1779667200LL, false},
  {"Pilates", 1779696000LL, 1779699600LL, false},
  {"Pilates", 1779955200LL, 1779958800LL, false},
  {"Payday", 1780005600LL, 1780005600LL, false},
  {"Drinks", 1780039800LL, 1780047000LL, false},
  {"Rent", 1780182000LL, 1780182000LL, false},
  {"Month 31", 1780192800LL, 1780194600LL, false},
  {"Team meeting", 1780268400LL, 1780272000LL, false},
};

std::string crlf(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (c == '\n') out += '\r';
    out += c;
  }
  return out;
}

struct Loaded {
  ICalCalendar calendar;
  ICalParser parser{calendar};
  bool closed = false;
  Loaded() { calendar.setWindow(kFrom, kTo); }
  void feed(const std::string &text, size_t chunk) {
    for (size_t at = 0; at < text.size(); at += chunk) parser.feed(text.data() + at, text.size() - at < chunk ? text.size() - at : chunk);
    closed = parser.finish();
  }
  size_t list(ICalOccurrence *out, size_t max, bool *more = nullptr) {
    MelbourneDevice device;
    return calendar.occurrences(kFrom, kTo, device, out, max, more);
  }
};

bool matchesReference(const ICalOccurrence *got, size_t n) {
  const size_t want = sizeof(kExpected) / sizeof(kExpected[0]);
  if (n != want) {
    fprintf(stderr, "  %zu occurrences, expected %zu\n", n, want);
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    const Expect &e = kExpected[i];
    if (strcmp(got[i].summary, e.summary) != 0 || got[i].start != e.start || got[i].end != e.end || got[i].allDay != e.allDay) {
      fprintf(stderr, "  #%zu: got \"%s\" %lld-%lld, expected \"%s\" %lld-%lld\n", i, got[i].summary, (long long)got[i].start,
              (long long)got[i].end, e.summary, (long long)e.start, (long long)e.end);
      return false;
    }
  }
  return true;
}

}  // namespace

TEST(zone_rules_follow_the_real_clock_changes) {
  struct Capture : ICalSink {
    ICalZone zones[2];
    int count = 0;
    void onEvent(const ICalEvent &, const ICalZone *z) override {
      if (!z) return;
      for (int i = 0; i < count; ++i)
        if (strcmp(zones[i].tzid, z->tzid) == 0) return;
      if (count < 2) zones[count++] = *z;
    }
  } capture;
  ICalParser parser(capture);
  const std::string text(kCalendar);
  parser.feed(text.data(), text.size());
  CHECK(parser.finish());
  const ICalZone *mel = nullptr, *ny = nullptr;
  for (int i = 0; i < capture.count; ++i) (strcmp(capture.zones[i].tzid, "Australia/Melbourne") == 0 ? mel : ny) = &capture.zones[i];
  CHECK(mel && ny);
  if (!mel || !ny) return;
  // {instant of the change, offset before, offset after}: from zoneinfo, 2026 to 2029.
  const int64_t kMel[][3] = {{1775318400, 39600, 36000}, {1791043200, 36000, 39600}, {1806768000, 39600, 36000},
                             {1822492800, 36000, 39600}, {1838217600, 39600, 36000}, {1853942400, 36000, 39600},
                             {1869667200, 39600, 36000}};
  const int64_t kNy[][3] = {{1772953200, -18000, -14400}, {1793512800, -14400, -18000}, {1805007600, -18000, -14400},
                            {1825567200, -14400, -18000}, {1836457200, -18000, -14400}, {1857016800, -14400, -18000},
                            {1867906800, -18000, -14400}};
  for (const auto &t : kMel) {
    CHECK_EQ(mel->offsetAtUtc(t[0] - 1), t[1]);
    CHECK_EQ(mel->offsetAtUtc(t[0]), t[2]);
  }
  for (const auto &t : kNy) {
    CHECK_EQ(ny->offsetAtUtc(t[0] - 1), t[1]);
    CHECK_EQ(ny->offsetAtUtc(t[0]), t[2]);
  }
  // Wall to instant: ordinary, ambiguous (clocks go back at 03:00 -> 02:00) and skipped (02:00 -> 03:00).
  const int64_t noon = civil::wallSeconds(2026, 7, 1, 12, 0, 0);
  CHECK_EQ(mel->toUtc(noon), noon - 36000);
  const int64_t twice = civil::wallSeconds(2026, 4, 5, 2, 30, 0);  // happens at +11, and again at +10
  CHECK_EQ(mel->toUtc(twice), twice - 39600);                      // the first one
  const int64_t never = civil::wallSeconds(2026, 10, 4, 2, 30, 0);
  CHECK_EQ(mel->toUtc(never), never - 36000);  // read with the offset from before the change
  CHECK_EQ(mel->toWall(mel->toUtc(noon)), noon);
}

TEST(a_realistic_calendar_matches_the_reference_occurrence_by_occurrence) {
  Loaded l;
  l.feed(kCalendar, 100000);
  CHECK(l.closed);
  ICalOccurrence out[128];
  bool more = true;
  const size_t n = l.list(out, 128, &more);
  CHECK(!more);
  CHECK(matchesReference(out, n));
  const ICalParser::Stats &s = l.parser.stats();
  CHECK_EQ(s.events, 17u);        // every well-formed VEVENT, overrides included (the broken date is skipped)
  CHECK_EQ(s.skipped, 1u);
  CHECK_EQ(s.unsupportedRules, 1u);  // FREQ=HOURLY: a single occurrence, noted
  CHECK_EQ(s.unresolvedZones, 0u);
  CHECK_EQ(s.zonesDropped, 0u);
  CHECK_EQ(l.calendar.dropped(), 0u);
  CHECK_EQ(l.calendar.outsideWindow(), 1u);  // "Long ago"
}

TEST(the_same_calendar_whatever_the_chunking_line_endings_or_bom) {
  const std::string lf(kCalendar);
  for (size_t chunk : {1, 2, 3, 7, 64, 333}) {
    Loaded l;
    l.feed(crlf(lf), chunk);
    CHECK(l.closed);
    ICalOccurrence out[128];
    CHECK(matchesReference(out, l.list(out, 128)));
  }
  Loaded plain;
  plain.feed(lf, 41);
  ICalOccurrence a[128];
  CHECK(matchesReference(a, plain.list(a, 128)));
  Loaded bom;
  bom.feed(std::string("\xef\xbb\xbf") + crlf(lf), 5);
  ICalOccurrence b[128];
  CHECK(matchesReference(b, bom.list(b, 128)));
}

TEST(folded_lines_escapes_and_utf8_survive) {
  Loaded l;
  l.feed(crlf(kCalendar), 3);
  ICalOccurrence out[128];
  const size_t n = l.list(out, 128);
  bool found = false;
  for (size_t i = 0; i < n; ++i)
    if (strstr(out[i].summary, "Zo") != nullptr) {
      found = true;
      CHECK(strcmp(out[i].summary, "Caf\xc3\xa9 \xe2\x98\x95, with Zo\xc3\xab; room 4") == 0);
    }
  CHECK(found);
}

TEST(windows_can_be_asked_for_in_pieces_and_capped) {
  Loaded l;
  l.feed(kCalendar, 1000);
  MelbourneDevice device;
  ICalOccurrence out[8];
  bool more = false;
  // The first week only.
  const size_t week = l.calendar.occurrences(kFrom, kFrom + 7 * 86400, device, out, 8, &more);
  CHECK(week == 8 && more);  // a busy week: eight fit, and it says there were more
  for (size_t i = 1; i < week; ++i) CHECK(out[i - 1].start <= out[i].start);
  CHECK(strcmp(out[0].summary, "Team meeting") == 0);
  // Later than everything.
  CHECK_EQ(l.calendar.occurrences(kTo + 400 * 86400, kTo + 401 * 86400, device, out, 8, &more), 0u);
  // Reversed or empty windows give nothing and do not crash.
  CHECK_EQ(l.calendar.occurrences(kTo, kFrom, device, out, 8, &more), 0u);
  CHECK_EQ(l.calendar.occurrences(kFrom, kTo, device, out, 0, &more), 0u);
}

TEST(long_running_series_are_expanded_without_walking_every_period) {
  struct Case {
    const char *rule;
    int y0, m0, d0;  // DTSTART date at 07:00 UTC
    int fromY, fromM, fromD, toY, toM, toD;
    int expectedDays[8][3];  // up to 8 expected dates; y == 0 ends the list
  };
  const Case cases[] = {
      {"FREQ=DAILY;COUNT=1000", 2024, 1, 1, 2026, 9, 20, 2026, 9, 30, {{2026, 9, 20}, {2026, 9, 21}, {2026, 9, 22}, {2026, 9, 23}, {2026, 9, 24}, {2026, 9, 25}, {2026, 9, 26}, {0, 0, 0}}},
      {"FREQ=DAILY;INTERVAL=3", 1990, 1, 1, 2026, 11, 1, 2026, 11, 12, {{2026, 11, 3}, {2026, 11, 6}, {2026, 11, 9}, {0, 0, 0}}},
      {"FREQ=WEEKLY;INTERVAL=3;BYDAY=TU,SA", 1999, 3, 2, 2026, 11, 1, 2026, 12, 25, {{2026, 11, 17}, {2026, 11, 21}, {2026, 12, 8}, {2026, 12, 12}, {0, 0, 0}}},
      {"FREQ=MONTHLY;INTERVAL=3;BYMONTHDAY=31", 1990, 1, 31, 2026, 1, 1, 2027, 6, 1, {{2026, 1, 31}, {2026, 7, 31}, {2026, 10, 31}, {2027, 1, 31}, {0, 0, 0}}},
      {"FREQ=YEARLY;INTERVAL=4;BYMONTH=2;BYMONTHDAY=29", 1996, 2, 29, 2020, 1, 1, 2032, 12, 31, {{2020, 2, 29}, {2024, 2, 29}, {2028, 2, 29}, {2032, 2, 29}, {0, 0, 0}}},
  };
  for (const Case &c : cases) {
    char text[600];
    snprintf(text, sizeof(text),
             "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:x\r\nSUMMARY:S\r\nDTSTART:%04d%02d%02dT070000Z\r\nRRULE:%s\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n",
             c.y0, c.m0, c.d0, c.rule);
    ICalCalendar calendar;
    ICalParser parser(calendar);
    parser.feed(text, strlen(text));
    CHECK(parser.finish());
    UtcResolver utc;
    ICalOccurrence out[16];
    const int64_t from = civil::daysFromCivil(c.fromY, c.fromM, c.fromD) * 86400;
    const int64_t to = civil::daysFromCivil(c.toY, c.toM, c.toD) * 86400;
    const size_t n = calendar.occurrences(from, to, utc, out, 16);
    size_t want = 0;
    while (c.expectedDays[want][0] != 0) ++want;
    CHECK_EQ(n, want);
    for (size_t i = 0; i < n && i < want; ++i) {
      CHECK_EQ(out[i].start, civil::wallSeconds(c.expectedDays[i][0], c.expectedDays[i][1], c.expectedDays[i][2], 7, 0, 0));
    }
  }
}

TEST(a_huge_calendar_streams_in_bounded_memory) {
  // 20,000 events (about 7 MB), mostly in the past; the calendar keeps only this week's.
  ICalCalendar calendar;
  calendar.setWindow(kFrom, kFrom + 14 * 86400);
  ICalParser parser(calendar);
  CHECK(sizeof(ICalParser) < 4096 && sizeof(ICalCalendar) < 9000);
  const char *head = "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n";
  parser.feed(head, strlen(head));
  for (int i = 0; i < 20000; ++i) {
    char ev[600];
    // Two thirds long ago; some in the window; the DESCRIPTION is long and folded.
    const int year = i % 3 == 0 ? 2026 : 2005 + i % 15;
    const int day = 1 + i % 28;
    int n = snprintf(ev, sizeof(ev),
                     "BEGIN:VEVENT\r\nUID:e%d\r\nSUMMARY:Event %d\r\nDTSTART:%04d03%02dT090000Z\r\nDTEND:%04d03%02dT100000Z\r\n"
                     "DESCRIPTION:Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore\r\n"
                     " et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex\r\n"
                     " ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat.\r\n"
                     "END:VEVENT\r\n",
                     i, i, year, day, year, day);
    // arbitrary chunking, including one byte at a time for a few
    const int chunk = i % 50 == 0 ? 1 : 997;
    for (int at = 0; at < n; at += chunk) parser.feed(ev + at, n - at < chunk ? n - at : chunk);
  }
  const char *tail = "END:VCALENDAR\r\n";
  parser.feed(tail, strlen(tail));
  CHECK(parser.finish());
  CHECK_EQ(parser.stats().events, 20000u);
  CHECK(parser.stats().cutLines > 0);  // the long DESCRIPTIONs were cut, not kept
  CHECK(calendar.size() <= ICalCalendar::kMaxEvents);
  CHECK(calendar.size() + calendar.dropped() + calendar.outsideWindow() == 20000u);
  CHECK(calendar.size() > 0 && calendar.dropped() > 0);  // more in the window than there is room for: counted, not lost silently
  UtcResolver utc;
  ICalOccurrence out[40];
  const size_t n = calendar.occurrences(kFrom, kFrom + 14 * 86400, utc, out, 40);
  CHECK(n > 0 && n <= calendar.size());
  for (size_t i = 1; i < n; ++i) CHECK(out[i - 1].start <= out[i].start);
}

TEST(damaged_input_never_crashes_and_says_so) {
  const std::string good = crlf(kCalendar);
  // Cut off anywhere: whatever was read is fine, and the end is noticed.
  for (size_t cut = 0; cut < good.size(); cut += 37) {
    Loaded l;
    l.feed(good.substr(0, cut), 64);
    CHECK(!l.closed);
    CHECK(l.parser.error() != nullptr);
  }
  // Not a calendar at all.
  {
    Loaded l;
    l.feed("<html><body>404 Not Found</body></html>\r\n", 8);
    CHECK(!l.closed && l.parser.error() && strstr(l.parser.error(), "Not an iCalendar") != nullptr);
    CHECK_EQ(l.calendar.size(), 0u);
  }
  // Random bytes after a valid start, and every byte value.
  {
    Loaded l;
    uint32_t x = 99;
    std::string junk = "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\n";
    for (int i = 0; i < 50000; ++i) {
      x = x * 1664525u + 1013904223u;
      junk += (char)(x >> 24);
    }
    l.feed(junk, 211);
    ICalOccurrence out[8];
    l.list(out, 8);
  }
  // Absurd structure: very deep nesting, one enormous line, endless parameters.
  {
    std::string s = "BEGIN:VCALENDAR\r\n";
    for (int i = 0; i < 100; ++i) s += "BEGIN:X-DEEP\r\n";
    s += "BEGIN:VEVENT\r\nDTSTART:20260305T090000Z\r\nSUMMARY:deep\r\nEND:VEVENT\r\n";
    for (int i = 0; i < 100; ++i) s += "END:X-DEEP\r\n";
    s += "BEGIN:VEVENT\r\nSUMMARY:" + std::string(100000, 'x') + "\r\nDTSTART:20260305T090000Z\r\nEND:VEVENT\r\n";
    s += "BEGIN:VEVENT\r\nDTSTART;" + std::string(5000, 'A') + "=b;TZID=Nowhere/Land:20260306T090000\r\nSUMMARY:ok\r\nEND:VEVENT\r\n";
    s += "END:VCALENDAR\r\n";
    Loaded l;
    l.feed(s, 500);
    CHECK(l.closed);
    CHECK(l.parser.stats().cutLines >= 2);
    ICalOccurrence out[8];
    const size_t n = l.list(out, 8);
    CHECK(n >= 1);
    for (size_t i = 0; i < n; ++i) CHECK(strlen(out[i].summary) < sizeof(ICalEvent::summary));
  }
}

TEST(unknown_zones_and_bad_dates_are_noted_not_trusted) {
  const std::string text =
      "BEGIN:VCALENDAR\r\n"
      "BEGIN:VEVENT\r\nUID:a\r\nSUMMARY:Mars\r\nDTSTART;TZID=Mars/Olympus:20260305T090000\r\nDTEND;TZID=Mars/Olympus:20260305T100000\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:b\r\nSUMMARY:Feb 30\r\nDTSTART:20260230T090000Z\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:c\r\nSUMMARY:Hour 25\r\nDTSTART:20260305T250000Z\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:d\r\nSUMMARY:No start\r\nEND:VEVENT\r\n"
      "BEGIN:VEVENT\r\nUID:e\r\nSUMMARY:Backwards\r\nDTSTART:20260305T090000Z\r\nDTEND:20260305T080000Z\r\nEND:VEVENT\r\n"
      "END:VCALENDAR\r\n";
  Loaded l;
  l.feed(text, 16);
  CHECK(l.closed);
  const ICalParser::Stats &s = l.parser.stats();
  CHECK_EQ(s.unresolvedZones, 1u);  // read as the device's own time, and said so
  CHECK_EQ(s.skipped, 3u);          // Feb 30, hour 25, no start
  CHECK_EQ(s.events, 2u);
  ICalOccurrence out[8];
  const size_t n = l.list(out, 8);
  CHECK_EQ(n, 2u);
  // Mars is read as Melbourne time (the device's zone), 09:00 AEDT = 22:00Z the day before.
  CHECK_EQ(out[0].start, civil::wallSeconds(2026, 3, 5, 9, 0, 0) - 39600);
  CHECK_EQ(out[0].end - out[0].start, 3600);
  // An end before the start is a zero-length event, not a negative one.
  CHECK_EQ(out[1].end, out[1].start);
}

TEST(rules_with_every_part_agree_with_the_reference_on_a_dense_month) {
  // Weekly by day on a Sunday week start, monthly by the second Tuesday, yearly by month and ordinal:
  // each checked against python-dateutil for all of 2026-2027.
  struct Case {
    const char *rule;
    int count;
    int64_t first, last;  // the first and last occurrence (UTC, 07:00) in 2026-2027
  };
  UtcResolver utc;
  const Case cases[] = {
      {"FREQ=WEEKLY;BYDAY=SU,WE;WKST=SU", 208, civil::wallSeconds(2026, 1, 4, 7, 0, 0), civil::wallSeconds(2027, 12, 29, 7, 0, 0)},
      {"FREQ=MONTHLY;BYDAY=2TU", 24, civil::wallSeconds(2026, 1, 13, 7, 0, 0), civil::wallSeconds(2027, 12, 14, 7, 0, 0)},
      {"FREQ=MONTHLY;BYDAY=MO,FR;BYMONTHDAY=13", 8, civil::wallSeconds(2026, 2, 13, 7, 0, 0), civil::wallSeconds(2027, 12, 13, 7, 0, 0)},
      {"FREQ=YEARLY;BYMONTH=11;BYDAY=-1TH", 2, civil::wallSeconds(2026, 11, 26, 7, 0, 0), civil::wallSeconds(2027, 11, 25, 7, 0, 0)},
  };
  for (const Case &c : cases) {
    char text[400];
    snprintf(text, sizeof(text),
             "BEGIN:VCALENDAR\nBEGIN:VEVENT\nUID:x\nSUMMARY:S\nDTSTART:20250101T070000Z\nRRULE:%s\nEND:VEVENT\nEND:VCALENDAR\n", c.rule);
    ICalCalendar calendar;
    ICalParser parser(calendar);
    parser.feed(text, strlen(text));
    CHECK(parser.finish());
    std::vector<ICalOccurrence> out(300);
    const size_t n = calendar.occurrences(civil::daysFromCivil(2026, 1, 1) * 86400, civil::daysFromCivil(2028, 1, 1) * 86400, utc, out.data(), 300);
    CHECK_EQ((int)n, c.count);
    if (n) {
      CHECK_EQ(out[0].start, c.first);
      CHECK_EQ(out[n - 1].start, c.last);
    }
  }
}
