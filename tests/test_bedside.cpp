// The bedside clock: a fullscreen idle face that takes over after the idle
// dim at night, shows the next alarm, and leaves with a tap.
#include <string.h>

#include "sim_session.h"
#include "stella/ui/layout.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

const char *topApp(SimSession &s) { return s.os.foreground() ? s.os.foreground()->info().id : ""; }

bool sameAsFullRedraw(SimSession &s) {
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
  CHECK(s.dev.display.pixel(10, s.os.theme().m.statusBar / 2) == bg);
  CHECK(sameAsFullRedraw(s));
  // The backlight is lower than the plain dim.
  SimSession plain(320, 240);
  quickDim(plain, 0);
  plain.run(31000);
  CHECK(s.dev.display.backlight() < plain.dev.display.backlight());
  // The waking tap leaves for the Clock; the status bar is back.
  s.tap(160, 120);
  CHECK(!isBedside(s) && strcmp(topApp(s), "clock") == 0 && s.os.power() == PowerState::Active);
  CHECK(s.dev.display.pixel(10, s.os.theme().m.statusBar / 2) != s.os.theme().color.bedBg);
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
  // Each minute only the time is repainted, not the whole screen.
  s.run(3000);
  s.dev.display.resetCounters();
  s.run(61 * 1000);
  CHECK(s.dev.display.pixelsWritten() < 320u * 240u / 2 && s.dev.display.writes() >= 1);
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

TEST(display_screen_bedside_row_sets_the_mode) {
  SimSession s(320, 240);
  s.os.launch("settings");
  s.run(100);
  const Point row = s.settingsRow(1);
  s.tap(row.x, row.y);  // Display
  const Theme &t = s.os.theme();
  const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  const int want = t.m.control + t.dp(4), fit = (area.h - 2 * t.dp(4)) / 6;
  const int rowH = want <= fit ? want : fit < t.dp(32) ? t.dp(32) : fit;
  const int y = area.y + t.dp(4) + 4 * rowH + rowH / 2;
  CHECK(s.os.settings().bedside == 1);
  s.tap(area.right() - t.m.padding - t.dp(20), y);  // "Always" is the last chip
  CHECK(s.os.settings().bedside == 2);
  s.tap(area.x + area.w * 3 / 4 - t.dp(30), y);
  CHECK(s.os.settings().bedside != 2);
}
