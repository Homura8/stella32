// What's new after an update, and the customisable Home screen.
#include <string.h>

#include "sim_session.h"
#include "stella/apps/changelog.h"
#include "stella/net/firmware_update.h"
#include "stella/ui/layout.h"
#include "test.h"

using namespace stella;
using namespace stella::host;
using namespace stella::apps;

namespace {

// A runtime booted from storage the test prepared: a device that had settings
// (or not), a record of the version last seen, a saved Home layout.
struct Boot {
  SimDevice dev{320, 240};
  RuntimeConfig cfg;
  Runtime os;
  LauncherApp launcher;
  ClockApp clock;
  TimerApp timer;
  StopwatchApp stopwatch;
  SettingsApp settings;
  TouchTestApp touch;

  Boot() {
    dev.platform.setEpoch(kMorningEpoch);
    cfg.renderBuffer = dev.buffer.data();
    cfg.renderBufferPixels = dev.buffer.size();
    cfg.serviceThread = false;
  }
  void start(Runtime &runtime) {
    runtime.begin(dev.board(), cfg);
    runtime.install(launcher);
    runtime.install(clock);
    runtime.install(timer);
    runtime.install(stopwatch);
    runtime.install(settings);
    runtime.install(touch);
    runtime.start();
  }
  void begin() { start(os); }
  void run(Runtime &runtime, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 50) {
      runtime.step();
      runtime.serviceStep();
      dev.platform.advance(50);
    }
  }
  void run(uint32_t ms) { run(os, ms); }
  void setSeen(const char *version) {
    char text[16] = {0};
    strncpy(text, version, 15);
    saveRecord(&dev.storage, "sys", "seen", 1, text, sizeof(text));
  }
  void hadSettings() { saveSettings(&dev.storage, SystemSettings{}); }
  std::string seen() {
    char text[16] = {0};
    return loadRecord(&dev.storage, "sys", "seen", 1, text, sizeof(text)) == RecordStatus::Ok ? text : "";
  }
};

const char *topTitle(Runtime &os) { return os.topScreen() && os.topScreen()->title() ? os.topScreen()->title() : ""; }
const char *foregroundId(Runtime &os) { return os.foreground() ? os.foreground()->info().id : ""; }

void tapRect(SimSession &s, const Rect &r) { s.tap(r.centerX(), r.centerY()); }

}  // namespace

TEST(whats_new_opens_once_after_an_update_and_is_remembered) {
  Boot b;
  b.hadSettings();
  b.setSeen("0.9.0");  // updated from an older version
  b.begin();
  b.run(2000);
  CHECK(strcmp(foregroundId(b.os), "home") == 0);  // not at once: let Home settle
  b.run(6000);
  CHECK(strcmp(foregroundId(b.os), "settings") == 0 && strcmp(topTitle(b.os), "What's new") == 0);
  CHECK(b.seen() == STELLA_VERSION);
  // The next boot (same storage): nothing to say.
  Runtime again;
  b.start(again);
  b.run(again, 9000);
  CHECK(strcmp(foregroundId(again), "home") == 0);
}

TEST(a_new_device_and_a_rollback_do_not_show_it) {
  {
    Boot fresh;  // no saved settings: nothing to compare with
    fresh.begin();
    fresh.run(9000);
    CHECK(strcmp(foregroundId(fresh.os), "home") == 0);
    CHECK(fresh.seen() == STELLA_VERSION);  // but it learns the version
  }
  {
    Boot rolledBack;  // an older version after a rollback: the notes would be for the wrong one
    rolledBack.hadSettings();
    rolledBack.setSeen("99.0.0");
    rolledBack.begin();
    rolledBack.run(9000);
    CHECK(strcmp(foregroundId(rolledBack.os), "home") == 0);
  }
  {
    Boot same;
    same.hadSettings();
    same.setSeen(STELLA_VERSION);
    same.begin();
    same.run(9000);
    CHECK(strcmp(foregroundId(same.os), "home") == 0);
  }
}

TEST(whats_new_waits_until_home_is_in_front) {
  Boot b;
  b.hadSettings();
  b.setSeen("0.9.0");
  b.begin();
  b.os.launch("clock");  // the person is already somewhere else
  b.run(9000);
  CHECK(strcmp(foregroundId(b.os), "clock") == 0);
  b.os.goHome();
  b.run(1000);
  CHECK(strcmp(foregroundId(b.os), "settings") == 0 && strcmp(topTitle(b.os), "What's new") == 0);
}

TEST(the_changelog_is_current_in_order_and_fits_the_screen) {
  int count = 0;
  const ChangelogEntry *log = changelog(count);
  CHECK(count >= 5);
  CHECK(strcmp(log[0].version, STELLA_VERSION) == 0);  // bump the changelog with the version
  for (int i = 0; i + 1 < count; ++i)
    CHECK(UpdateService::compareVersions(log[i].version, log[i + 1].version) == 1);  // newest first
  for (int i = 0; i < count; ++i) {
    CHECK(log[i].lines[0] != nullptr);
    for (const char *line : log[i].lines)
      if (line) CHECK(strlen(line) <= 30);  // one line at 320 px, beside the scroll buttons
  }
}

TEST(about_opens_whats_new_and_it_renders_at_every_size) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    SimSession s(sz[0], sz[1], kMorningEpoch);
    s.os.launch("settings", "whatsnew");
    s.run(200);
    CHECK(strcmp(topTitle(s.os), "What's new") == 0);
    const std::vector<Color> incremental = s.dev.display.framebuffer();
    s.os.invalidateAll();
    s.run(20);
    CHECK(incremental == s.dev.display.framebuffer());
    s.os.pop();
    const Theme &t = s.os.theme();
    const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
    const Point about = s.settingsRow(5);
    s.tap(about.x, about.y);
    Rect r[2], b[2];
    layout::column(area, {layout::flex(), layout::px(t.m.control)}, t.m.gapS, r);
    layout::row(r[1], {layout::flex(), layout::flex()}, t.m.gapM, b);
    tapRect(s, b[0]);  // What's new
    CHECK(strcmp(topTitle(s.os), "What's new") == 0);
  }
}

// ------------------------------------------------------------------ Home screen editor

TEST(home_shows_every_app_in_install_order_by_default) {
  SimSession s(320, 240);
  const HomeLayoutRecord &l = s.launcher.layout();
  // The Touch test is not on Home at all (Settings > Display > Touch opens it); Stats is
  // there to be turned on but starts off; everything else shows.
  const char *expected[] = {"clock", "weather", "alarms", "timer", "stopwatch", "settings", "values", "calendar"};
  CHECK_EQ(l.count, 8);
  for (int i = 0; i < 8; ++i) CHECK(strcmp(l.items[i].id, expected[i]) == 0);
  for (int i = 0; i < 8; ++i) CHECK_EQ(l.items[i].visible != 0, strcmp(l.items[i].id, "values") != 0);
}

TEST(hiding_and_moving_apps_changes_home_and_is_saved) {
  SimSession s(480, 320, kMorningEpoch);  // tall enough for every row without scrolling
  s.os.launch("home", "customise");
  s.run(200);
  CHECK(strcmp(topTitle(s.os), "Home screen") == 0);
  const int timer = 3, stopwatch = 4;
  tapRect(s, s.launcher.editToggle(timer));  // hide Timer
  CHECK(!s.launcher.layout().items[timer].visible);
  tapRect(s, s.launcher.editUp(stopwatch));  // Stopwatch above Timer
  CHECK(strcmp(s.launcher.layout().items[3].id, "stopwatch") == 0 && strcmp(s.launcher.layout().items[4].id, "timer") == 0);
  tapRect(s, s.launcher.editDown(0));  // Clock below Weather
  CHECK(strcmp(s.launcher.layout().items[0].id, "weather") == 0);
  // It is saved...
  HomeLayoutRecord saved;
  CHECK(loadRecord(&s.dev.storage, "home", "layout", HomeLayoutRecord::kVersion, &saved, sizeof(saved)) == RecordStatus::Ok);
  CHECK(strcmp(saved.items[0].id, "weather") == 0 && saved.items[4].visible == 0);
  // ... and Home follows it: weather, clock, alarms, stopwatch, [timer hidden], settings, touch.
  s.os.pop();
  s.run(200);
  tapRect(s, s.launcher.tileBounds(0));
  CHECK(strcmp(s.os.foreground()->info().id, "weather") == 0);
  s.os.goHome();
  s.run(200);
  tapRect(s, s.launcher.tileBounds(3));
  CHECK(strcmp(s.os.foreground()->info().id, "stopwatch") == 0);
  s.os.goHome();
  s.run(200);
  tapRect(s, s.launcher.tileBounds(4));
  CHECK(strcmp(s.os.foreground()->info().id, "settings") == 0);  // Timer is not on Home
}

TEST(settings_cannot_be_hidden_from_home) {
  SimSession s(480, 320);
  s.os.launch("home", "customise");
  s.run(200);
  tapRect(s, s.launcher.editToggle(5));
  CHECK(s.launcher.layout().items[5].visible);
  CHECK(strcmp(s.launcher.layout().items[5].id, "settings") == 0);
}

TEST(a_saved_layout_is_merged_with_the_apps_that_exist) {
  Boot b;
  HomeLayoutRecord saved;
  const struct { const char *id; int visible; } items[] = {{"timer", 0}, {"ghost", 1}, {"clock", 1}, {"timer", 1}};
  for (const auto &it : items) {
    strncpy(saved.items[saved.count].id, it.id, 15);
    saved.items[saved.count++].visible = (uint8_t)it.visible;
  }
  CHECK(saveRecord(&b.dev.storage, "home", "layout", HomeLayoutRecord::kVersion, &saved, sizeof(saved)));
  b.begin();
  b.run(500);
  const HomeLayoutRecord &l = b.launcher.layout();
  // The saved order first (the unknown app dropped, the duplicate ignored), the rest appended.
  CHECK(strcmp(l.items[0].id, "timer") == 0 && !l.items[0].visible);
  CHECK(strcmp(l.items[1].id, "clock") == 0 && l.items[1].visible);
  CHECK_EQ(l.count, 4);  // timer, clock, then stopwatch and settings (the hidden Touch test never shows)
  for (int i = 2; i < l.count; ++i) CHECK(l.items[i].visible);
}

TEST(long_pressing_the_time_and_the_settings_row_open_the_editor) {
  {
    SimSession s(320, 240, kMorningEpoch);
    const Rect time = s.launcher.timeBounds();
    s.longPress(time.centerX(), time.centerY());
    CHECK(strcmp(topTitle(s.os), "Home screen") == 0);
  }
  SimSession s(800, 480, kMorningEpoch);  // every row of Settings fits without scrolling
  s.os.launch("settings");
  s.run(200);
  const Point row = s.settingsRow(6);  // Home screen
  s.tap(row.x, row.y);
  s.run(200);
  CHECK(strcmp(topTitle(s.os), "Home screen") == 0);
  CHECK(strcmp(s.os.foreground()->info().id, "home") == 0);
}

TEST(the_editor_and_settings_list_render_at_every_size) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    SimSession s(sz[0], sz[1], kMorningEpoch);
    s.os.launch("settings");
    s.run(200);
    CHECK(s.settingsRow(0).y > 0);
    CHECK(s.dev.display.framebuffer().size() > 0);
    const std::vector<Color> a = s.dev.display.framebuffer();
    s.os.invalidateAll();
    s.run(20);
    CHECK(a == s.dev.display.framebuffer());
    s.os.launch("home", "customise");
    s.run(200);
    CHECK(strcmp(topTitle(s.os), "Home screen") == 0);
    CHECK(s.launcher.editRow(0).h >= s.os.theme().dp(32));
    const std::vector<Color> b = s.dev.display.framebuffer();
    s.os.invalidateAll();
    s.run(20);
    CHECK(b == s.dev.display.framebuffer());
  }
}

// What a person new to the OS sees: a Home with the useful apps, no developer tools, and the
// optional ones one tap away in Settings.
TEST(a_new_person_sees_only_useful_apps_and_finds_the_touch_test_in_settings) {
  SimSession s(320, 240);
  // On Home: no Touch test, no Stats (it starts off); the others are there.
  for (int i = 0; i < 8; ++i) {
    const Rect tile = s.launcher.tileBounds(i);
    if (tile.empty()) continue;
    s.tap(tile.centerX(), tile.centerY());
    CHECK(strcmp(s.os.foreground()->info().id, "touch") != 0 && strcmp(s.os.foreground()->info().id, "values") != 0);
    s.os.goHome();
    s.run(100);
  }
  // Settings > Display > Touch opens it (and it is still launchable by name).
  s.os.launch("settings");
  s.run(100);
  const Point display = s.settingsRow(1);
  s.tap(display.x, display.y);
  const Theme &t = s.os.theme();
  const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  const int want = t.m.control + t.dp(4), fit = (area.h - 2 * t.dp(4)) / 6;
  const int rowH = want <= fit ? want : fit < t.dp(32) ? t.dp(32) : fit;
  s.tap(area.centerX(), area.y + t.dp(4) + 5 * rowH + rowH / 2);  // the sixth row: Touch
  CHECK(strcmp(s.os.foreground()->info().id, "touch") == 0);
  // Stats can be turned on from the Home screen editor, where it starts off.
  s.os.goHome();
  s.os.launch("home", "customise");
  s.run(200);
  bool listed = false;
  for (int i = 0; i < s.launcher.layout().count; ++i)
    if (strcmp(s.launcher.layout().items[i].id, "values") == 0) listed = !s.launcher.layout().items[i].visible;
  CHECK(listed);
}

// A person new to the OS: after Wi-Fi, Home asks once for a time zone (the clock and the
// weather follow it), and the weather is for that zone's city until a city is chosen.
TEST(a_new_device_asks_for_a_time_zone_once) {
  SimSession s(320, 240, kEveningEpoch, Appearance::Auto, false, /*online=*/true, /*withDeck=*/false, /*newDevice=*/true);
  s.os.goHome();
  s.run(500);
  CHECK(!s.os.status().zoneChosen);
  tapRect(s, s.launcher.noteBounds());  // "Choose your time zone"
  CHECK(strcmp(foregroundId(s.os), "settings") == 0);
  CHECK(strcmp(topTitle(s.os), "Time") == 0);
  CHECK(s.os.status().zoneChosen);  // opening the screen is enough: it has been seen
  uint8_t chosen = 0;
  CHECK(loadRecord(&s.dev.storage, "sys", "setup", 1, &chosen, sizeof(chosen)) == RecordStatus::Ok && chosen == 1);
  // Home no longer asks: its note is the moon again, and tapping it does nothing.
  s.os.goHome();
  s.run(500);
  tapRect(s, s.launcher.noteBounds());
  CHECK(strcmp(foregroundId(s.os), "home") == 0);
}

TEST(a_device_with_settings_already_saved_is_never_asked_for_a_zone) {
  SimSession s(320, 240);  // the simulator's ordinary device does not ask
  CHECK(s.os.status().zoneChosen);
}

TEST(the_weather_follows_the_time_zone_until_a_city_is_chosen) {
  SimSession s(320, 240);
  CHECK(strstr(s.dev.http.lastUrl.c_str(), "latitude=-37.81") != nullptr);  // Melbourne
  SystemSettings st = s.os.settings();
  snprintf(st.timezone, sizeof(st.timezone), "%s", "JST-9");
  s.os.applySettings(st);
  s.os.launch("weather");
  s.run(2000);
  CHECK(strstr(s.dev.http.lastUrl.c_str(), "latitude=35.68") != nullptr);  // Tokyo, with no step in between
  // A city picked by hand stays, whatever the zone does afterwards.
  s.os.launch("weather", "places");
  const Rect c = s.os.contentRect();
  const Theme &t = s.os.theme();
  s.tap(c.centerX(), c.y + t.m.gapS + t.dp(4) + 0 * t.dp(30) + t.dp(15));  // row 0
  s.run(500);
  const std::string picked = s.dev.http.lastUrl;
  st.timezone[0] = 'E'; st.timezone[1] = 'S'; st.timezone[2] = 'T'; st.timezone[3] = '5'; st.timezone[4] = 0;
  s.os.applySettings(st);
  s.os.launch("weather");
  s.run(2000);
  CHECK(s.dev.http.lastUrl == picked);
}
