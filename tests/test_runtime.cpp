// Runtime integration: navigation, shell, power, persistence, supervision,
// rendering invariants and layouts across display sizes.
#include <string.h>

#include "sim_session.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

const char *topApp(SimSession &s) { return s.os.foreground() ? s.os.foreground()->info().id : ""; }

// Centre of launcher tile `i` (first page), derived from the same theme tokens.
Point launcherTile(SimSession &s, int index) {
  const Theme &t = s.os.theme();
  const Rect c = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  const int columns = t.density == Density::Regular ? 5 : 4;
  const int tileW = (c.w - t.m.gapM * (columns - 1)) / columns;  // square tiles
  const int top = c.y + t.font.headline->lineHeight + t.font.caption->lineHeight + 2 * t.m.gapS;
  return Point{(int16_t)(c.x + index * (tileW + t.m.gapM) + tileW / 2), (int16_t)(top + tileW / 2)};
}

}  // namespace

TEST(boots_into_launcher_and_launches_apps_by_touch) {
  SimSession s(320, 240);
  CHECK(strcmp(topApp(s), "home") == 0);
  CHECK(!s.os.renderPending());
  const Point clockTile = launcherTile(s, 0);
  s.tap(clockTile.x, clockTile.y);
  CHECK(strcmp(topApp(s), "clock") == 0);
  const Point back = s.backButton();
  s.tap(back.x, back.y);
  CHECK(strcmp(topApp(s), "home") == 0);
}

TEST(settings_navigation_uses_the_screen_stack) {
  SimSession s(320, 240);
  s.os.launch("settings");
  s.run(50);
  const Point row = s.settingsRow(1);
  s.tap(row.x, row.y);
  CHECK(s.os.topScreen() && strcmp(s.os.topScreen()->title(), "Display") == 0);
  const Point back = s.backButton();
  s.tap(back.x, back.y);
  CHECK(strcmp(topApp(s), "settings") == 0 && s.os.topScreen() == &s.settings.root());
  s.longPress(s.os.screenRect().centerX(), 8);  // long-press the status bar: home
  CHECK(strcmp(topApp(s), "home") == 0);
}

TEST(settings_persist_after_debounce_and_reload) {
  SimSession s(320, 240);
  SystemSettings changed = s.os.settings();
  changed.appearance = Appearance::Light;
  changed.brightness = 90;
  changed.nightDim = false;  // the fixture is at night
  const size_t writesBefore = s.dev.storage.writes();
  s.os.applySettings(changed);
  CHECK(!s.os.theme().color.dark);  // applied immediately
  CHECK_EQ(s.dev.storage.writes(), writesBefore);  // ...but not written yet
  s.run(2000);
  CHECK_EQ(s.dev.storage.writes(), writesBefore + 1);
  CHECK_EQ(s.dev.display.backlight(), backlightDuty(90));
  // A fresh runtime on the same storage starts with the saved settings.
  SystemSettings loaded;
  CHECK(loadSettings(&s.dev.storage, loaded) == RecordStatus::Ok);
  CHECK(loaded.appearance == Appearance::Light && loaded.brightness == 90);
}

TEST(theme_follows_the_sky) {
  SimSession evening(320, 240, kEveningEpoch);
  CHECK(evening.os.theme().color.dark && evening.os.theme().phase == SkyPhase::Night);
  SimSession morning(320, 240, kMorningEpoch);
  CHECK(!morning.os.theme().color.dark && morning.os.theme().phase == SkyPhase::Day);
  SimSession unknown(320, 240, 0);
  CHECK(unknown.os.theme().phase == SkyPhase::Night);
  // Crossing into dusk re-themes on the minute check.
  morning.dev.platform.setEpoch(kMorningEpoch + 7 * 3600 + 50 * 60);  // 17:05
  morning.run(100);
  CHECK(morning.os.theme().phase == SkyPhase::Dusk && morning.os.theme().color.dark);
}

TEST(power_dims_sleeps_and_wake_touch_is_consumed) {
  SimSession s(320, 240);
  SystemSettings p = s.os.settings();
  p.dimAfterSec = 30;
  p.sleepAfterSec = 60;
  s.os.applySettings(p);
  s.run(31000);
  CHECK(s.os.power() == PowerState::Dimmed && s.dev.display.backlight() < backlightDuty(p.brightness));
  s.run(30000);
  CHECK(s.os.power() == PowerState::Off && s.dev.display.backlight() == 0);
  // The waking tap must not launch the tile under the finger.
  const Point tile = launcherTile(s, 0);
  s.tap(tile.x, tile.y);
  CHECK(s.os.power() == PowerState::Active && strcmp(topApp(s), "home") == 0);
  s.tap(tile.x, tile.y);
  CHECK(strcmp(topApp(s), "clock") == 0);
}

TEST(night_dimming_caps_the_backlight) {
  SimSession night(320, 240, kEveningEpoch);
  CHECK_EQ(night.dev.display.backlight(), backlightDuty(nightBrightness(night.os.settings().brightness)));
  SystemSettings s = night.os.settings();
  s.nightDim = false;
  night.os.applySettings(s);
  CHECK_EQ(night.dev.display.backlight(), backlightDuty(s.brightness));
  SimSession day(320, 240, kMorningEpoch);
  CHECK_EQ(day.dev.display.backlight(), backlightDuty(day.os.settings().brightness));
}

// Regression (CYD): with the default 60 s dim, a tap on the dimmed (still
// readable) screen was swallowed, so apps seemed to need several taps.
TEST(tap_on_a_dimmed_screen_brightens_and_acts) {
  SimSession s(320, 240);
  SystemSettings p = s.os.settings();
  p.dimAfterSec = 30;
  p.nightDim = false;  // compare with the plain brightness
  s.os.applySettings(p);
  s.run(31000);
  CHECK(s.os.power() == PowerState::Dimmed);
  const Point tile = launcherTile(s, 0);
  s.tap(tile.x, tile.y);
  CHECK(s.os.power() == PowerState::Active && s.dev.display.backlight() == backlightDuty(p.brightness));
  CHECK(strcmp(topApp(s), "clock") == 0);
}

// Regression (CYD): the press highlight costs a frame; if the finger lifts
// during it, the gesture sees one sample. The tile must still open.
TEST(tap_seen_once_across_a_slow_frame_still_launches) {
  SimSession s(320, 240);
  const Point tile = launcherTile(s, 0);
  s.dev.touch.down(tile.x, tile.y);
  s.os.step();                  // Press: the tile highlight is rendered
  s.dev.platform.advance(150);  // ... slowly, and the finger lifts meanwhile
  s.dev.touch.up();
  s.run(200);
  CHECK(strcmp(topApp(s), "clock") == 0);
}

// Regression (CYD): with the Wi-Fi setup card the home grid is paged, and
// paging needed a swipe, which is hard on a resistive panel.
TEST(home_pages_with_buttons) {
  SimSession s(320, 240, kEveningEpoch, Appearance::Auto, false, /*online=*/false);
  const Theme &t = s.os.theme();
  const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  const int headerTop = area.y, headerBottom = area.y + t.font.headline->lineHeight + t.m.gapS + t.font.caption->lineHeight;
  const int y = (headerTop + headerBottom) / 2;
  const int nextX = area.right() - t.dp(88) / 4, prevX = area.right() - t.dp(88) * 3 / 4;
  s.tap(nextX, y);
  // Page 2 starts with the fifth app; its tile is now first.
  const int columns = 4;
  const int tileW = (area.w - t.m.gapM * (columns - 1)) / columns;
  const int tileY = headerBottom + t.m.gapS + tileW / 2;
  s.tap(area.x + tileW / 2, tileY);
  CHECK(strcmp(topApp(s), "settings") == 0);  // the fifth listed app
  s.os.goHome();
  s.run(100);
  s.tap(prevX, y);
  s.tap(area.x + tileW / 2, tileY);
  CHECK(strcmp(topApp(s), "clock") == 0);
}

TEST(toast_expires_and_repaints) {
  SimSession s(320, 240);
  s.os.toast("Hello");
  s.run(50);
  CHECK(!s.os.renderPending());
  s.run(3000);
  CHECK(!s.os.renderPending());
  // After expiry the toast area matches a clean redraw.
  std::vector<Color> afterToast = s.dev.display.framebuffer();
  s.os.invalidateAll();
  s.run(20);
  CHECK(afterToast == s.dev.display.framebuffer());
}

TEST(idle_screens_do_not_redraw) {
  SimSession s(320, 240);
  s.os.launch("clock");
  s.run(200);
  s.dev.display.resetCounters();
  s.run(10000);  // the clock face only changes at the minute boundary
  CHECK(s.dev.display.pixelsWritten() < 320u * 240u / 4);
}

TEST(incremental_frames_match_full_redraw) {
  SimSession s(320, 240);
  s.os.launch("settings");
  s.run(100);
  const Point row = s.settingsRow(1);
  s.tap(row.x, row.y);  // Display screen
  const Rect c = s.os.contentRect();
  s.drag(c.x + 30, c.y + 45, c.x + 150, c.y + 45);  // move the brightness slider
  s.run(200);
  const std::vector<Color> incremental = s.dev.display.framebuffer();
  s.os.invalidateAll();
  s.run(20);
  CHECK(incremental == s.dev.display.framebuffer());
}

TEST(supervisor_restarts_a_stalled_ui) {
  SimSession s(320, 240, kEveningEpoch, Appearance::Auto, /*threaded=*/true);
  // The UI loop stops running; the service thread must notice and restart.
  s.dev.platform.advance(s.config.uiStallLimitMs + 1000);
  for (int i = 0; i < 300 && s.dev.platform.restarts() == 0; ++i) osal::sleepMs(10);
  CHECK_EQ(s.dev.platform.restarts(), 1);
  size_t size = 0;
  RestartRecord &r = *static_cast<RestartRecord *>(s.dev.platform.retainedMemory(size));
  CHECK(restart_record::valid(r) && strcmp(r.thread, "ui") == 0 && r.pending == 1);
  s.os.step();
  CHECK_EQ(s.dev.platform.restarts(), 1);  // requested once only
}

TEST(app_records_are_namespaced) {
  SimSession s(320, 240);
  s.os.launch("clock");
  const int v = 7;
  CHECK(s.os.saveAppRecord("prefs", 1, &v, sizeof(v)));
  int out = 0;
  CHECK(loadRecord(&s.dev.storage, "clock", "prefs", 1, &out, sizeof(out)) == RecordStatus::Ok && out == 7);
}

TEST(every_screen_renders_at_every_size) {
  // Portrait CYD, CYD, 3.5" 480x320, 4.3" 800x480.
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    SimSession s(sz[0], sz[1]);
    for (const char *app : {"home", "clock", "weather", "alarms", "timer", "settings", "touch"}) {
      s.os.launch(app);
      s.run(100);
      CHECK(!s.os.renderPending());
      // Something besides the sky was drawn in the content area.
      CHECK(s.dev.display.framebuffer().size() == (size_t)sz[0] * sz[1]);
    }
    s.os.launch("settings");
    for (int i = 0; i < 5; ++i) {
      const Point row = s.settingsRow(i);
      s.tap(row.x, row.y);
      CHECK(s.os.topScreen() != &s.settings.root());
      const Point back = s.backButton();
      s.tap(back.x, back.y);
      CHECK(s.os.topScreen() == &s.settings.root());
    }
  }
}
