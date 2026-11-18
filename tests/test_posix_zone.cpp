// The device's POSIX time zone as a thread-safe zone: every preset in Settings
// is checked against zoneinfo at each clock change from 2026 to 2028.
#include <initializer_list>
#include <string.h>

#include "stella/net/posix_zone.h"
#include "test.h"

using namespace stella;

namespace {

struct Change {
  int64_t at;  // the instant of the change (UTC)
  int64_t before, after;  // offsets east of UTC
};

struct Preset {
  const char *tz;
  Change changes[6];
};

// From python's zoneinfo (Europe/London, Europe/Paris, America/New_York, America/Los_Angeles,
// Australia/Melbourne, Pacific/Auckland).
const Preset kPresets[] = {
    {"GMT0BST,M3.5.0/1,M10.5.0", {{1774746000, 0, 3600}, {1792890000, 3600, 0}, {1806195600, 0, 3600}, {1824944400, 3600, 0}, {1837645200, 0, 3600}, {1856394000, 3600, 0}}},
    {"CET-1CEST,M3.5.0,M10.5.0/3", {{1774746000, 3600, 7200}, {1792890000, 7200, 3600}, {1806195600, 3600, 7200}, {1824944400, 7200, 3600}, {1837645200, 3600, 7200}, {1856394000, 7200, 3600}}},
    {"EST5EDT,M3.2.0,M11.1.0", {{1772953200, -18000, -14400}, {1793512800, -14400, -18000}, {1805007600, -18000, -14400}, {1825567200, -14400, -18000}, {1836457200, -18000, -14400}, {1857016800, -14400, -18000}}},
    {"PST8PDT,M3.2.0,M11.1.0", {{1772964000, -28800, -25200}, {1793523600, -25200, -28800}, {1805018400, -28800, -25200}, {1825578000, -25200, -28800}, {1836468000, -28800, -25200}, {1857027600, -25200, -28800}}},
    {"AEST-10AEDT,M10.1.0,M4.1.0/3", {{1775318400, 39600, 36000}, {1791043200, 36000, 39600}, {1806768000, 39600, 36000}, {1822492800, 36000, 39600}, {1838217600, 39600, 36000}, {1853942400, 36000, 39600}}},
    {"NZST-12NZDT,M9.5.0,M4.1.0/3", {{1775311200, 46800, 43200}, {1790431200, 43200, 46800}, {1806760800, 46800, 43200}, {1821880800, 43200, 46800}, {1838210400, 46800, 43200}, {1853330400, 43200, 46800}}},
};

}  // namespace

TEST(posix_zones_change_at_the_same_instants_as_the_real_ones) {
  for (const Preset &p : kPresets) {
    ICalZone zone;
    CHECK(parsePosixZone(p.tz, zone));
    for (const Change &c : p.changes) {
      CHECK_EQ(zone.offsetAtUtc(c.at - 1), c.before);
      CHECK_EQ(zone.offsetAtUtc(c.at), c.after);
    }
  }
}

TEST(posix_zones_without_daylight_saving_are_constant) {
  const struct { const char *tz; int32_t offset; } kFixed[] = {
      {"UTC0", 0}, {"JST-9", 9 * 3600}, {"<+08>-8", 8 * 3600}, {"AEST-10", 10 * 3600}, {"<-0330>3:30", -(3 * 3600 + 1800)},
      {"IST-5:30", 5 * 3600 + 1800}};
  for (const auto &f : kFixed) {
    ICalZone zone;
    CHECK(parsePosixZone(f.tz, zone));
    for (int64_t t : {0LL, 1767225600LL, 1782864000LL, 1800000000LL}) CHECK_EQ(zone.offsetAtUtc(t), f.offset);
    PosixZoneResolver r;
    CHECK(r.set(f.tz));
    CHECK_EQ(r.localToEpoch(1782864000LL), 1782864000LL - f.offset);
  }
}

TEST(posix_zone_resolver_reads_wall_clock_time_on_both_sides_of_a_change) {
  PosixZoneResolver r;
  CHECK(r.set("AEST-10AEDT,M10.1.0,M4.1.0/3"));
  CHECK_EQ(r.localToEpoch(civil::wallSeconds(2026, 7, 1, 12, 0, 0)), civil::wallSeconds(2026, 7, 1, 12, 0, 0) - 36000);
  CHECK_EQ(r.localToEpoch(civil::wallSeconds(2026, 12, 1, 12, 0, 0)), civil::wallSeconds(2026, 12, 1, 12, 0, 0) - 39600);
  CHECK_EQ(r.epochToLocal(1775318400 - 1), 1775318400 - 1 + 39600);
  CHECK_EQ(r.epochToLocal(1775318400), 1775318400 + 36000);
}

TEST(posix_zone_strings_that_are_not_understood_fall_back_to_utc) {
  for (const char *bad : {"", "AEST", "AEST-10AEDT,M10.1.0", "AEST-10AEDT,J100,J300", "X-1", "AEST-10AEDT,M13.1.0,M4.1.0",
                          "AEST-10AEDT,M10.6.0,M4.1.0", "AEST-10AEDT,M10.1.0,M4.1.0/3junk", "<+08-8", "EST5EDT,"}) {
    PosixZoneResolver r;
    ICalZone zone;
    CHECK(!parsePosixZone(bad, zone));
    CHECK(!r.set(bad));
    CHECK_EQ(r.localToEpoch(1000000), 1000000);  // UTC
  }
  ICalZone zone;
  CHECK(parsePosixZone(nullptr, zone) == false);
  // With no rules after the daylight name, the old US rules apply.
  CHECK(parsePosixZone("EST5EDT", zone));
  CHECK_EQ(zone.offsetAtUtc(1782864000LL), -14400);  // 1 July
  CHECK_EQ(zone.offsetAtUtc(1767225600LL), -18000);  // 1 January
}
