// The bedside clock: a fullscreen idle face that takes over after the idle
// dim at night, shows the next alarm, and leaves with a tap.
#include <string.h>

#include "sim_session.h"
#include "stella/apps/common.h"
#include "stella/ui/layout.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

const char *topApp(SimSession &s) { return s.os.foreground() ? s.os.foreground()->info().id : ""; }

// Animation frames and the minute change on their own, so a comparison with
// a full redraw first waits until neither is about to happen.
bool nearFrameEdge(uint32_t ms) {
  const uint32_t phase = ms % 16000u;
  for (uint32_t edge : {0u, 4000u, 8000u, 12000u, 15000u})
    if (phase >= edge && phase - edge < 150) return true;  // just changed (the tick lands a few ms later)
  return apps::bedsideMsToNextFrame(ms) <= 600;           // about to change
}

void settle(SimSession &s) {
  for (int i = 0; i < 100; ++i) {
    const LocalTime now = s.os.localTime();
    if (!nearFrameEdge(s.dev.platform.millis()) && now.second < 55) break;
    s.run(100);
  }
}

bool sameAsFullRedraw(SimSession &s) {
  settle(s);
  const std::vector<Color> incremental = s.dev.display.framebuffer();
  s.os.invalidateAll();
  s.run(20);
  return incremental == s.dev.display.framebuffer();
}

SystemSettings quickDim(SimSession &s, uint8_t bedside) {
  SystemSettings p = s.os.settings();
  p.dimAfterSec = 30;
  p.bedside = bedside;
  s.os.applySettings(p);
  return p;
}

bool luma_over(Color c, int level) { return (red8(c) * 299 + green8(c) * 587 + blue8(c) * 114) / 1000 > level; }

bool isBedside(SimSession &s) { return s.os.topScreen() && s.os.topScreen()->fullscreen(); }

}  // namespace

TEST(bedside_takes_over_after_the_idle_dim_at_night_and_a_tap_leaves) {
  SimSession s(320, 240);  // Monday 22:30
  quickDim(s, 1);
  s.run(31000);
  CHECK(isBedside(s) && strcmp(topApp(s), "clock") == 0);
  // Fullscreen: no sky and no status bar, just the flat bedside colour behind the text.
  const Color bg = s.os.theme().color.bedBg;
  CHECK(s.dev.display.pixel(2, 2) == bg && s.dev.display.pixel(318, 237) == bg);
  CHECK(s.dev.display.pixel(160, 3) == bg);
  CHECK(sameAsFullRedraw(s));
  // The backlight is lower than the plain dim.
  SimSession plain(320, 240);
  quickDim(plain, 0);
  plain.run(31000);
  CHECK(s.dev.display.backlight() < plain.dev.display.backlight());
  // The waking tap leaves for the Clock; the status bar is back.
  s.tap(160, 120);
  CHECK(!isBedside(s) && strcmp(topApp(s), "clock") == 0 && s.os.power() == PowerState::Active);
  CHECK(s.dev.display.pixel(160, 3) != s.os.theme().color.bedBg);
  CHECK(sameAsFullRedraw(s));
  // Idle again: it comes back.
  s.run(31000);
  CHECK(isBedside(s));
}

TEST(bedside_modes_off_night_and_always) {
  SimSession off(320, 240);
  quickDim(off, 0);
  off.run(31000);
  CHECK(!isBedside(off) && strcmp(topApp(off), "home") == 0);
  SimSession dayNight(320, 240, kMorningEpoch);
  quickDim(dayNight, 1);
  dayNight.run(31000);
  CHECK(!isBedside(dayNight));  // "Night" waits for the night
  SimSession always(320, 240, kMorningEpoch);
  quickDim(always, 2);
  always.run(31000);
  CHECK(isBedside(always));
  SimSession noTime(320, 240, /*epoch=*/0, Appearance::Auto, false, /*online=*/false);
  quickDim(noTime, 2);
  noTime.run(31000);
  CHECK(!isBedside(noTime));  // nothing to show without the time
}

TEST(bedside_never_replaces_what_the_user_is_doing) {
  SimSession s(320, 240);
  quickDim(s, 2);
  s.os.launch("settings");
  s.run(100);
  s.run(31000);
  CHECK(!isBedside(s) && strcmp(topApp(s), "settings") == 0);
  // From the Clock it does start, and leaving returns to the Clock's own screen.
  SimSession c(320, 240);
  quickDim(c, 2);
  c.os.launch("clock");
  c.run(31000);
  CHECK(isBedside(c));
  c.tap(100, 100);
  CHECK(!isBedside(c) && c.os.topScreen() == &c.clock.root());
  // An alert wakes the screen and shows over the bedside clock.
  c.run(31000);
  CHECK(isBedside(c));
  Alert a;
  a.title = "Hello";
  CHECK(c.os.showAlert(a));
  CHECK(c.os.alertShowing() && c.os.power() == PowerState::Active);
}

TEST(bedside_button_enters_it_manually_and_idle_faces_do_not_stack) {
  SimSession s(320, 240, kMorningEpoch);
  s.os.launch("clock");
  s.run(300);
  const Theme &t = s.os.theme();
  Rect parts[6];
  const Font &display = *t.font.display;
  layout::column(s.os.contentRect().inset(t.m.margin, t.m.gapS),
                 {layout::flex(), layout::px(display.ascent + t.dp(4)), layout::px(t.font.title->lineHeight),
                  layout::px(t.m.control), layout::px(t.m.control), layout::flex()},
                 t.m.gapS, parts);
  s.tap(parts[4].centerX(), parts[4].centerY());
  CHECK(isBedside(s));
  // Entering again (a second route) does not stack another bedside screen.
  s.os.launch("clock", "bedside");
  s.tap(100, 100);
  CHECK(!isBedside(s) && s.os.topScreen() == &s.clock.root());
}

TEST(bedside_shows_the_next_alarm_and_follows_the_minute) {
  SimSession s(320, 240);  // Monday 22:30
  s.os.launch("alarms");
  s.run(100);
  apps::AlarmEntry a;
  a.hour = 6;
  a.minute = 45;
  a.days = 0;
  CHECK(s.alarms.setAlarm(-1, a, s.os));
  s.run(100);
  CHECK(s.os.status().nextAlarm > 0);
  const LocalTime at = s.os.localTimeAt(s.os.status().nextAlarm);
  CHECK(at.hour == 6 && at.minute == 45 && at.second == 0);
  // The Alarms app hands the time to the shell; toggling it off clears it.
  s.os.goHome();
  s.os.launch("clock", "bedside");
  s.run(100);
  const std::vector<Color> withAlarm = s.dev.display.framebuffer();
  s.os.launch("alarms");
  s.run(100);
  s.alarms.setAlarm(0, [] { apps::AlarmEntry e; e.hour = 6; e.minute = 45; e.enabled = 0; return e; }(), s.os);
  CHECK(s.os.status().nextAlarm == 0);
  s.os.launch("clock", "bedside");
  s.run(100);
  CHECK(withAlarm != s.dev.display.framebuffer());  // the alarm line is gone
  // A whole minute of animation and the minute change repaint a couple of
  // screens' worth in total (small spots every four seconds, not whole frames).
  s.run(3000);
  s.dev.display.resetCounters();
  s.run(61 * 1000);
  CHECK(s.dev.display.pixelsWritten() < 5u * 320u * 240u / 2u && s.dev.display.writes() >= 1);
  CHECK(sameAsFullRedraw(s));
}

TEST(next_alarm_includes_a_snooze_and_clears_when_stopped) {
  SimSession s(320, 240);
  s.os.launch("alarms");
  s.run(100);
  apps::AlarmEntry a;
  a.hour = 22;
  a.minute = 31;
  a.days = 0;
  CHECK(s.alarms.setAlarm(-1, a, s.os));
  s.run(70 * 1000);
  CHECK(s.os.alertShowing());
  const Rect snooze = s.os.alertButtonRect(1);
  s.tap(snooze.centerX(), snooze.centerY());
  CHECK(s.os.status().nextAlarm > 0);  // the snooze ring
  s.run(9 * 60 * 1000 + 2000);
  CHECK(s.os.alertShowing());
  const Rect stop = s.os.alertButtonRect(0);
  s.tap(stop.centerX(), stop.centerY());
  CHECK(s.os.status().nextAlarm == 0);  // the one-off alarm is spent
}

TEST(bedside_renders_at_every_size_and_with_a_12_hour_clock) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    for (bool h24 : {true, false}) {
      SimSession s(sz[0], sz[1]);
      SystemSettings p = s.os.settings();
      p.clock24h = h24;
      s.os.applySettings(p);
      s.os.launch("clock", "bedside");
      s.run(300);
      CHECK(isBedside(s) && !s.os.renderPending());
      CHECK(s.dev.display.pixel(1, 1) == s.os.theme().color.bedBg);
      CHECK(sameAsFullRedraw(s));
    }
  }
}

TEST(settings_v5_records_are_migrated_with_the_board_bedside_default) {
  MemoryStore store;
  struct V5 {
    Appearance appearance;
    uint8_t brightness;
    uint16_t dimAfterSec, sleepAfterSec;
    bool clock24h;
    char timezone[48];
    bool nightDim;
    ColorTone colorTone;
    uint8_t contrastLight, contrastDark, whiteLevel, warmth;
    bool dither;
    int8_t panelVcom;
    uint8_t panelRefreshHz;
    uint8_t skyPin;
    int16_t touchScaleX, touchOffsetX, touchScaleY, touchOffsetY;
  } v5{Appearance::Dark, 130, 60, 0, true, "AEST-10", true, ColorTone::Rich, 86, 74, 70, 30, true, 0, 0, 0,
       1024, 7, 1024, 0};
  CHECK(saveRecord(&store, "sys", "settings", 5, &v5, sizeof(v5)));
  SystemSettings s;
  s.bedside = 2;  // a board default
  bool migrated = false;
  CHECK(loadSettings(&store, s, &migrated) == RecordStatus::Ok && migrated);
  CHECK(s.contrastLight == 86 && s.contrastDark == 74 && s.whiteLevel == 70 && s.touchOffsetX == 7);
  CHECK(s.bedside == 2);
}

TEST(idle_face_screen_sets_when_and_which_face) {
  SimSession s(320, 240);
  s.os.launch("settings", "idle");  // Settings > Idle face (the last row of the list)
  s.run(100);
  CHECK(s.os.topScreen() && strcmp(s.os.topScreen()->title(), "Idle face") == 0);
  CHECK(s.os.settings().bedside == 1 && s.os.settings().idleFace == 0);
  const Rect always = s.settings.idleChoice(0, 2);  // Start: Off | Night | Always
  s.tap(always.centerX(), always.centerY());
  CHECK(s.os.settings().bedside == 2);
  const Rect off = s.settings.idleChoice(0, 0);
  s.tap(off.centerX(), off.centerY());
  CHECK(s.os.settings().bedside == 0);
  const Rect dashboard = s.settings.idleChoice(1, 1);  // Face: Clock | Dashboard
  s.tap(dashboard.centerX(), dashboard.centerY());
  CHECK(s.os.settings().idleFace == 1);
  const Rect clock = s.settings.idleChoice(1, 0);
  s.tap(clock.centerX(), clock.centerY());
  CHECK(s.os.settings().idleFace == 0);
}

TEST(bedside_animation_cycle_and_calm_repaints) {
  using apps::bedsideFrameAt;
  using apps::bedsideMsToNextFrame;
  // The 16 s cycle: twinkle on the second and fourth 4 s, one blink at the end.
  CHECK_EQ(bedsideFrameAt(0), 0);
  CHECK_EQ(bedsideFrameAt(4500), 2);
  CHECK_EQ(bedsideFrameAt(9000), 0);
  CHECK_EQ(bedsideFrameAt(13000), 2);
  CHECK_EQ(bedsideFrameAt(15500), 3);
  CHECK_EQ(bedsideFrameAt(16000), 0);
  CHECK(bedsideMsToNextFrame(100) > 3900 && bedsideMsToNextFrame(14990) < 20 && bedsideMsToNextFrame(15500) < 600);
  // Over a full cycle only the twinkling spots repaint: less than one screen
  // in total (a full repaint on every frame would be five).
  SimSession s(320, 240, kEveningEpoch);
  s.os.launch("clock", "bedside");
  s.run(300);
  settle(s);
  while (bedsideFrameAt(s.dev.platform.millis()) != 0) s.run(50);  // start of a cycle
  s.dev.display.resetCounters();
  const LocalTime start = s.os.localTime();
  s.run(15900);  // the cycle, short of the minute change when we started early in it
  if (s.os.localTime().minute == start.minute) {
    CHECK(s.dev.display.writes() >= 4);
    CHECK(s.dev.display.pixelsWritten() < 320u * 240u);
  }
  CHECK(sameAsFullRedraw(s));
}

TEST(bedside_shows_the_weather_and_withdraws_it_when_old) {
  SimSession s(320, 240, kEveningEpoch);
  s.run(40 * 1000);  // the Weather app was never opened: it publishes from its own timer
  const SystemStatus st = s.os.status();
  CHECK(!st.glance.empty() && strncmp(st.glance.text, "17", 2) == 0 && st.glance.icon != nullptr);
  s.os.launch("clock", "bedside");
  s.run(300);
  const std::vector<Color> withWeather = s.dev.display.framebuffer();
  // Four hours without a refresh: the weather line is withdrawn, not shown as current.
  s.dev.platform.advance(4u * 60u * 60u * 1000u);
  s.os.step();
  CHECK(s.os.status().glance.empty());
  s.run(300);
  CHECK(withWeather != s.dev.display.framebuffer());
  SimSession offline(320, 240, kEveningEpoch, Appearance::Auto, false, /*online=*/false);
  offline.run(40 * 1000);
  CHECK(offline.os.status().glance.empty());  // nothing to show without data
}

TEST(bedside_uses_the_largest_clock_that_fits) {
  for (const auto &sz : {std::pair<int, int>{240, 320}, std::pair<int, int>{320, 240}, std::pair<int, int>{480, 320}}) {
    SimSession s(sz.first, sz.second);
    SystemSettings p = s.os.settings();
    p.clock24h = false;  // AM/PM must fit beside the digits
    s.os.applySettings(p);
    s.os.launch("clock", "bedside");
    s.run(300);
    // Nothing of the clock may touch the display edge.
    const Theme &t = s.os.theme();
    bool edgeTouched = false;
    for (int y = 0; y < sz.second && !edgeTouched; y += 2) {
      const Color left = s.dev.display.pixel(t.m.margin / 2, y), right = s.dev.display.pixel(sz.first - 1 - t.m.margin / 2, y);
      // Only the sparkles and the planet are allowed near the edges: they are dim.
      edgeTouched = luma_over(left, 150) || luma_over(right, 150);
    }
    CHECK(!edgeTouched);
  }
}

// ------------------------------------------------------------------ dashboard and sun

TEST(the_idle_face_setting_chooses_clock_or_dashboard) {
  SimSession clock(320, 240);  // Monday 22:30
  quickDim(clock, 1);
  clock.run(40000);
  CHECK(clock.os.topScreen() == &clock.clock.bedsideFace());
  SimSession board(320, 240);
  SystemSettings p = quickDim(board, 1);
  p.idleFace = 1;
  board.os.applySettings(p);
  board.run(40000);
  CHECK(board.os.topScreen() == &board.clock.dashboardFace() && strcmp(topApp(board), "clock") == 0);
  CHECK(isBedside(board));  // fullscreen, dimmed like the bedside clock
  CHECK(board.dev.display.pixel(2, 2) == board.os.theme().color.bedBg);
  board.tap(160, 120);  // a tap leaves
  CHECK(!isBedside(board) && board.os.topScreen() == &board.clock.root());
  board.run(40000);  // idle again: back to the dashboard
  CHECK(board.os.topScreen() == &board.clock.dashboardFace());
  // "Off" wins over the face: nothing starts.
  SimSession off(320, 240);
  p = quickDim(off, 0);
  p.idleFace = 1;
  off.os.applySettings(p);
  off.run(40000);
  CHECK(!isBedside(off));
}

TEST(dashboard_renders_everywhere_matches_a_full_redraw_and_stays_still) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    for (bool h24 : {true, false}) {
      SimSession s(sz[0], sz[1]);
      SystemSettings p = s.os.settings();
      p.clock24h = h24;
      s.os.applySettings(p);
      s.run(40000);  // the weather glance is published
      s.os.launch("clock", "dashboard");
      s.run(300);
      CHECK(s.os.topScreen() == &s.clock.dashboardFace());
      for (int i = 0; i < 40 && s.os.localTime().second >= 45; ++i) s.run(1000);
      const std::vector<Color> incremental = s.dev.display.framebuffer();
      s.os.invalidateAll();
      s.run(20);
      CHECK(incremental == s.dev.display.framebuffer());
      // Nothing moves: ten seconds later (inside the same minute) not one pixel was written.
      s.dev.display.resetCounters();
      s.run(10000);
      CHECK_EQ(s.dev.display.pixelsWritten(), 0u);
    }
  }
}

TEST(dashboard_updates_only_what_changed_at_the_minute) {
  SimSession s(320, 240);
  s.run(40000);
  s.os.launch("clock", "dashboard");
  s.run(300);
  while (s.os.localTime().second < 50) s.run(1000);
  s.dev.display.resetCounters();
  s.run(12000);  // across the minute change
  CHECK(s.dev.display.pixelsWritten() > 0 && s.dev.display.pixelsWritten() < 320u * 240u / 2);
}

TEST(the_next_sun_event_reads_sunrise_before_dawn_sunset_by_day) {
  // Melbourne, 16 November: sunrise 05:59, sunset 20:10.
  SimSession night(320, 240, kEveningEpoch);  // 22:30
  night.run(40000);
  char text[40];
  apps::describeNextSun(night.os, text, sizeof(text));
  CHECK(strcmp(text, "Sunrise 05:59") == 0);
  SimSession day(320, 240, kMorningEpoch);
  day.run(40000);
  apps::describeNextSun(day.os, text, sizeof(text));
  CHECK(strcmp(text, "Sunset 20:10") == 0);
  SystemSettings p = day.os.settings();
  p.clock24h = false;
  day.os.applySettings(p);
  apps::describeNextSun(day.os, text, sizeof(text));
  CHECK(strcmp(text, "Sunset 8:10 PM") == 0);
  // Without a time or a place's sun there is no line.
  SimSession none(320, 240, /*epoch=*/0, Appearance::Auto, false, /*online=*/false);
  none.run(1000);
  apps::describeNextSun(none.os, text, sizeof(text));
  CHECK(text[0] == '\0');
}

TEST(bedside_face_shows_the_sun_line_and_withdraws_it_with_the_weather_place) {
  SimSession s(320, 240, kEveningEpoch);
  s.run(40000);
  s.os.launch("clock", "bedside");
  s.run(300);
  const std::vector<Color> withSun = s.dev.display.framebuffer();
  s.os.setSun(0, 0, 0);  // unknown again
  s.run(2000);
  CHECK(s.dev.display.framebuffer() != withSun);
  CHECK(sameAsFullRedraw(s));
}
