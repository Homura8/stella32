// The Calendar app: the address, the fetch on the service thread, the agenda it
// shows, every way the feed or the network can fail, and the Settings screen.
#include <string.h>

#include <string>

#include "sim_session.h"
#include "stella/ui/layout.h"
#include "test.h"

using namespace stella;
using namespace stella::host;
using namespace stella::apps;

namespace {

const char *kUrl = "https://calendar.example/feeds/private-9f8e7d6c/basic.ics";

struct Expect {
  const char *title;
  int64_t start, end;
  bool allDay;
  const char *where;
};

// The simulator's feed, expanded by python-dateutil + zoneinfo for 16 November to 16 December
// 2026 in Melbourne (the first 16 occurrences; there are 32 in the 30 days).
const Expect kAgenda[] = {
    {"Team stand-up", 1794781800LL, 1794782700LL, false, ""},
    {"Chemistry lecture", 1794787200LL, 1794794400LL, false, "Room 3.12"},
    {"Dentist", 1794864600LL, 1794867300LL, false, ""},
    {"Team stand-up", 1794868200LL, 1794869100LL, false, ""},
    {"Study group", 1794888000LL, 1794895200LL, false, "Library, level 2"},
    {"Team stand-up", 1794954600LL, 1794955500LL, false, ""},
    {"Canberra trip", 1795006800LL, 1795352400LL, true, ""},
    {"Team stand-up", 1795041000LL, 1795041900LL, false, ""},
    {"Team stand-up", 1795127400LL, 1795128300LL, false, ""},
    {"Movie night", 1795163400LL, 1795170600LL, false, "Cinema Nova"},
    {"Team stand-up", 1795386600LL, 1795387500LL, false, ""},
    {"Chemistry lecture", 1795392000LL, 1795399200LL, false, "Room 3.12"},
    {"Team stand-up", 1795473000LL, 1795473900LL, false, ""},
    {"Team stand-up", 1795559400LL, 1795560300LL, false, ""},
    {"Team stand-up", 1795645800LL, 1795646700LL, false, ""},
    {"Team stand-up", 1795732200LL, 1795733100LL, false, ""},
};

bool matchesAgenda(const CalendarSnapshot &snap) {
  if (snap.count != sizeof(kAgenda) / sizeof(kAgenda[0])) return false;
  for (int i = 0; i < snap.count; ++i) {
    const CalendarItem &it = snap.items[i];
    const Expect &e = kAgenda[i];
    if (strcmp(it.title, e.title) != 0 || it.start != e.start || it.end != e.end || it.allDay != e.allDay ||
        strcmp(it.where, e.where) != 0) {
      fprintf(stderr, "  item %d: got \"%s\" %lld-%lld, expected \"%s\" %lld-%lld\n", i, it.title, (long long)it.start,
              (long long)it.end, e.title, (long long)e.start, (long long)e.end);
      return false;
    }
  }
  return true;
}

// A session on Monday 10:15 with the calendar address set and the first fetch done.
struct Fetched {
  SimSession s;
  explicit Fetched(int w = 320, int h = 240) : s(w, h, kMorningEpoch) {
    s.os.calendar().setAddress(kUrl);
    s.run(3000);
  }
};

}  // namespace

TEST(the_feed_is_fetched_expanded_in_the_device_zone_and_matches_the_reference) {
  Fetched f;
  const CalendarSnapshot snap = f.s.calendar.snapshot();
  CHECK(snap.hasAddress && snap.status.hasData && snap.status.lastOk);
  CHECK(matchesAgenda(snap));
  CHECK(snap.more);  // 32 events in the 30 days; 16 are kept
  CHECK_EQ(snap.events, 7);   // VEVENTs in the feed
  CHECK_EQ(snap.unreadable, 0);
  CHECK_EQ(snap.dropped, 0);
  CHECK_EQ(snap.from, 1794747600LL);  // Mon 16 Nov 00:00 AEDT
  CHECK(f.s.dev.http.lastUrl == kUrl);  // and nothing else was asked for it
}

TEST(no_address_means_no_request_and_a_gentle_message) {
  SimSession s(320, 240, kMorningEpoch);
  s.run(60000);
  CHECK(!s.os.calendar().hasAddress());
  CHECK(s.dev.http.lastUrl.find("calendar.example") == std::string::npos);
  const CalendarSnapshot snap = s.calendar.snapshot();
  CHECK(!snap.hasAddress && snap.count == 0 && !snap.status.hasData);
  s.os.launch("calendar");
  s.run(300);
  // "Set up" opens Settings > Calendar.
  const Rect setup = s.calendar.setupButton();
  CHECK(!setup.empty());
  s.tap(setup.centerX(), setup.centerY());
  CHECK(strcmp(s.os.foreground()->info().id, "settings") == 0);
  CHECK(s.os.topScreen() && strcmp(s.os.topScreen()->title(), "Calendar") == 0);
}

TEST(a_failing_feed_keeps_the_last_good_agenda_and_says_why) {
  Fetched f;
  CHECK(matchesAgenda(f.s.calendar.snapshot()));
  f.s.dev.http.failWith = -2;  // timed out
  f.s.os.calendar().refreshNow();
  f.s.run(3000);
  const CalendarSnapshot snap = f.s.calendar.snapshot();
  CHECK(matchesAgenda(snap));  // still there
  CHECK(snap.status.hasData && !snap.status.lastOk && snap.status.failures >= 1);
  CHECK(strstr(snap.status.error, "Timed out") != nullptr);
  // It recovers by itself.
  f.s.dev.http.failWith = 0;
  f.s.run(5 * 60 * 1000);
  CHECK(f.s.calendar.snapshot().status.lastOk && matchesAgenda(f.s.calendar.snapshot()));
}

TEST(a_page_that_is_not_a_calendar_is_refused_and_not_shown_as_one) {
  SimSession s(320, 240, kMorningEpoch);
  s.dev.http.routes.insert(s.dev.http.routes.begin(), {"notacalendar.example", "<html><body>Please sign in</body></html>", 200});
  s.os.calendar().setAddress("https://notacalendar.example/login");
  s.run(3000);
  const CalendarSnapshot snap = s.calendar.snapshot();
  CHECK(snap.hasAddress && snap.count == 0 && !snap.status.hasData);
  CHECK(strstr(snap.status.error, "Not an iCalendar") != nullptr);
  // A server error is reported as one.
  s.dev.http.routes.insert(s.dev.http.routes.begin(), {"broken.example", "oops", 500});
  s.os.calendar().setAddress("https://broken.example/a.ics");
  s.run(3000);
  CHECK(strstr(s.calendar.snapshot().status.error, "HTTP 500") != nullptr);
}

TEST(a_new_address_never_shows_the_old_ones_events) {
  Fetched f;
  CHECK(f.s.calendar.snapshot().count > 0);
  f.s.dev.http.routes.insert(f.s.dev.http.routes.begin(), {"broken.example", "oops", 500});
  f.s.os.calendar().setAddress("https://broken.example/a.ics");
  f.s.run(3000);
  const CalendarSnapshot snap = f.s.calendar.snapshot();
  CHECK(snap.count == 0 && !snap.status.hasData);
  f.s.os.calendar().clearAddress();
  f.s.run(3000);
  CHECK(!f.s.calendar.snapshot().hasAddress && f.s.calendar.snapshot().count == 0);
}

TEST(changing_the_time_zone_moves_all_day_events_and_the_cut_between_days) {
  Fetched f;
  const CalendarSnapshot melbourne = f.s.calendar.snapshot();
  SystemSettings st = f.s.os.settings();
  strcpy(st.timezone, "UTC0");
  f.s.os.applySettings(st);
  f.s.run(70000);  // the app notices within a minute and asks for a new fetch
  const CalendarSnapshot utc = f.s.calendar.snapshot();
  CHECK(utc.status.lastOk && utc.count > 0);
  int trip = -1, trip2 = -1;
  for (int i = 0; i < melbourne.count; ++i) if (strcmp(melbourne.items[i].title, "Canberra trip") == 0) trip = i;
  for (int i = 0; i < utc.count; ++i) if (strcmp(utc.items[i].title, "Canberra trip") == 0) trip2 = i;
  CHECK(trip >= 0 && trip2 >= 0);
  if (trip >= 0 && trip2 >= 0) {
    CHECK_EQ(melbourne.items[trip].start, 1795006800LL);                 // 19 Nov 00:00 AEDT
    CHECK_EQ(utc.items[trip2].start, 1795046400LL);                      // 19 Nov 00:00 UTC
    CHECK_EQ(utc.items[trip2].end - utc.items[trip2].start, 4 * 86400);  // still four days
  }
}

TEST(the_agenda_labels_days_and_dims_what_is_over) {
  Fetched f;
  f.s.os.launch("calendar");
  f.s.run(300);
  CHECK(!f.s.os.renderPending());
  // It matches a clean redraw, at every size and in both clock styles.
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    for (bool h24 : {true, false}) {
      Fetched g(sz[0], sz[1]);
      SystemSettings st = g.s.os.settings();
      st.clock24h = h24;
      g.s.os.applySettings(st);
      g.s.os.launch("calendar");
      g.s.run(300);
      const std::vector<Color> incremental = g.s.dev.display.framebuffer();
      g.s.os.invalidateAll();
      g.s.run(20);
      CHECK(incremental == g.s.dev.display.framebuffer());
      // Idle: nothing is repainted between minutes.
      while (g.s.os.localTime().second >= 45) g.s.run(1000);
      g.s.dev.display.resetCounters();
      g.s.run(8000);
      CHECK_EQ(g.s.dev.display.pixelsWritten(), 0u);
    }
  }
}

TEST(the_agenda_scrolls_with_its_buttons_when_it_overflows) {
  Fetched f;  // 16 events on a 320x240 screen
  f.s.os.launch("calendar");
  f.s.run(300);
  const Rect list = f.s.calendar.listBounds();
  CHECK(!list.empty());
  const std::vector<Color> top = f.s.dev.display.framebuffer();
  const Theme &t = f.s.os.theme();
  f.s.tap(list.right() - t.dp(16), list.bottom() - t.dp(16));  // the down button of the strip
  f.s.run(200);
  CHECK(f.s.dev.display.framebuffer() != top);
}

TEST(settings_calendar_shows_the_host_fetches_and_removes) {
  SimSession s(320, 240, kMorningEpoch);
  s.os.launch("settings", "calendar");
  s.run(100);
  CHECK(s.os.topScreen() && strcmp(s.os.topScreen()->title(), "Calendar") == 0);
  const Rect fetch = s.settings.calendarRow(1);
  const int before = s.dev.http.requests;
  s.tap(fetch.centerX(), fetch.centerY());  // nothing to fetch yet
  s.run(500);
  CHECK_EQ(s.dev.http.requests, before);
  s.os.calendar().setAddress(kUrl);
  s.run(3000);
  s.os.goHome();
  s.os.launch("settings", "calendar");
  s.run(100);
  const int requests = s.dev.http.requests;
  s.tap(fetch.centerX(), fetch.centerY());
  s.run(3000);
  CHECK(s.dev.http.requests > requests);
  const Rect remove = s.settings.calendarRow(2);
  s.tap(remove.centerX(), remove.centerY());
  CHECK(!s.os.calendar().hasAddress());
  s.run(3000);
  CHECK(!s.calendar.snapshot().hasAddress);
}

TEST(the_calendar_service_waits_for_the_time_instead_of_guessing) {
  SimSession s(320, 240, /*epoch=*/0, Appearance::Auto, false, /*online=*/true);
  s.os.calendar().setAddress(kUrl);
  s.run(5000);
  CHECK(s.calendar.snapshot().count == 0 && !s.calendar.snapshot().status.failures);
  s.dev.platform.setEpoch(kMorningEpoch);
  s.run(10000);
  CHECK(matchesAgenda(s.calendar.snapshot()));
}
