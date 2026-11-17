// Screen transitions: the shifted canvas view, the slide between screens, and
// every way it must stay out of the way (touch, a slow panel, the setting).
#include <string.h>

#include <vector>

#include "sim_session.h"
#include "stella/gfx/canvas.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

SimSession &animated(SimSession &s) {
  SystemSettings st = s.os.settings();
  st.animations = true;
  s.os.applySettings(st);
  s.run(50);
  return s;
}

bool sameAsFullRedraw(SimSession &s) {
  const std::vector<Color> incremental = s.dev.display.framebuffer();
  s.os.invalidateAll();
  s.run(20);
  return incremental == s.dev.display.framebuffer();
}

}  // namespace

TEST(shifted_canvas_draws_displaced_and_stays_clipped) {
  std::vector<Color> buffer(16 * 8, 0);
  Canvas c(buffer.data(), Rect(0, 0, 16, 8));
  Canvas v = c.shifted(5, 2);
  v.fillRect(Rect(1, 1, 3, 2), 0xFFFF);  // lands at (6,3) .. (8,4)
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 16; ++x) {
      const bool inside = x >= 6 && x <= 8 && y >= 3 && y <= 4;
      CHECK_EQ(buffer[y * 16 + x] == 0xFFFF, inside);
    }
  v.fillRect(Rect(-100, -100, 400, 400), 0x1234);  // everything: only the real pixels change
  CHECK_EQ(buffer[0], 0x1234);
  CHECK_EQ(buffer[16 * 8 - 1], 0x1234);
  // A narrowed clip is respected through the view, and the original is unaffected.
  std::vector<Color> other(16 * 8, 0);
  Canvas c2(other.data(), Rect(0, 0, 16, 8));
  const Rect previous = c2.pushClip(Rect(4, 0, 4, 8));
  Canvas v2 = c2.shifted(-3, 0);  // virtual x 7..10 is real x 4..7
  v2.fillRect(Rect(0, 0, 100, 1), 0xAAAA);
  for (int x = 0; x < 16; ++x) CHECK_EQ(other[x] == 0xAAAA, x >= 4 && x <= 7);
  c2.restoreClip(previous);
}

TEST(a_new_screen_slides_in_and_ends_exactly_like_a_cut) {
  SimSession cut(320, 240);
  cut.os.launch("settings");
  cut.run(600);
  SimSession s(320, 240);
  animated(s);
  s.os.launch("settings");
  CHECK(s.os.transitioning());
  s.run(60);
  CHECK(s.os.transitioning());
  const std::vector<Color> mid = s.dev.display.framebuffer();
  CHECK(mid != cut.dev.display.framebuffer());  // it is on its way
  s.run(600);
  CHECK(!s.os.transitioning());
  CHECK(s.dev.display.framebuffer() == cut.dev.display.framebuffer());  // and arrives identical
  CHECK(sameAsFullRedraw(s));
}

TEST(slide_directions_and_the_old_screen_moves_out) {
  SimSession s(320, 240);
  animated(s);
  const std::vector<Color> home = s.dev.display.framebuffer();
  s.os.launch("settings");
  s.run(40);
  CHECK(s.os.transitioning());
  const std::vector<Color> forward = s.dev.display.framebuffer();
  s.run(600);
  const std::vector<Color> settings = s.dev.display.framebuffer();
  // Going back slides the other way: the frame a little way in is not the
  // mirror of the forward one, and the end is the home screen again.
  s.os.goHome();
  s.run(40);
  CHECK(s.os.transitioning());
  const std::vector<Color> back = s.dev.display.framebuffer();
  CHECK(back != forward && back != settings && back != home);
  s.run(600);
  CHECK(!s.os.transitioning());
  CHECK(strcmp(s.os.foreground()->info().id, "home") == 0);
  CHECK(sameAsFullRedraw(s));
}

TEST(pushing_and_popping_inside_an_app_slide_too) {
  SimSession s(320, 240);
  s.os.launch("settings");
  s.run(100);
  animated(s);
  const Point row = s.settingsRow(0);
  s.tap(row.x, row.y);  // pushes Wi-Fi; 150 ms later it is still sliding in
  CHECK(s.os.topScreen()->title() && strcmp(s.os.topScreen()->title(), "Wi-Fi") == 0);
  CHECK(s.os.transitioning());
  s.run(600);
  CHECK(!s.os.transitioning());
  s.os.pop();
  CHECK(s.os.transitioning());
  s.run(600);
  CHECK(!s.os.transitioning() && s.os.topScreen() == &s.settings.root());
  CHECK(sameAsFullRedraw(s));
}

TEST(touch_during_a_slide_snaps_it_and_acts_on_the_final_screen) {
  SimSession s(320, 240);
  animated(s);
  s.os.launch("settings");
  s.run(30);
  CHECK(s.os.transitioning());
  const Point row = s.settingsRow(1);  // Display, where it will rest
  s.dev.touch.down(row.x, row.y);
  s.run(60);
  CHECK(!s.os.transitioning());
  s.dev.touch.up();
  s.run(600);
  CHECK(strcmp(s.os.topScreen()->title(), "Display") == 0);
}

TEST(no_slide_when_animations_are_off_or_a_screen_is_fullscreen) {
  SimSession s(320, 240);  // animations off in the simulator by default
  s.os.launch("settings");
  CHECK(!s.os.transitioning());
  animated(s);
  s.os.goHome();
  s.os.launch("clock");
  s.run(600);
  s.os.launch("clock", "bedside");  // fullscreen: a cut
  CHECK(!s.os.transitioning());
  s.run(300);
  s.os.goHome();  // and back out of it
  CHECK(!s.os.transitioning());
}

TEST(a_slow_panel_gets_cuts_not_a_stutter) {
  SimSession s(320, 240);
  animated(s);
  s.dev.display.onWrite = [&](const Rect &) { s.dev.platform.advance(12); };  // ~ 90 ms a full-width frame
  s.os.launch("settings");
  s.run(1000);
  CHECK(!s.os.transitioning());
  s.os.goHome();
  CHECK(!s.os.transitioning());  // and it stays off for the rest of the boot
  s.run(500);
  CHECK(sameAsFullRedraw(s));
}

TEST(transitions_work_at_every_size_and_survive_a_theme_change) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    SimSession s(sz[0], sz[1]);
    animated(s);
    s.os.launch("settings");
    s.run(40);
    CHECK(s.os.transitioning());
    SystemSettings st = s.os.settings();
    st.appearance = Appearance::Dark;  // re-themes while sliding: it must finish cleanly
    s.os.applySettings(st);
    s.run(600);
    CHECK(!s.os.transitioning());
    CHECK(sameAsFullRedraw(s));
    s.os.goHome();
    s.run(600);
    CHECK(sameAsFullRedraw(s));
  }
}

TEST(settings_v7_records_gain_animations_on) {
  MemoryStore store;
  struct V7 {
    Appearance appearance;
    uint8_t brightness;
    uint16_t dimAfterSec, sleepAfterSec;
    bool clock24h;
    char timezone[48];
    bool nightDim;
    uint8_t bedside;
    ColorTone colorTone;
    uint8_t contrastLight, contrastDark, whiteLevel, warmth;
    bool dither;
    int8_t panelVcom;
    uint8_t panelRefreshHz;
    uint8_t skyPin;
    int16_t touchScaleX, touchOffsetX, touchScaleY, touchOffsetY;
    uint8_t volume;
    bool autoUpdate;
  } v7{Appearance::Light, 140, 60, 0, true, "UTC0", true, 1, ColorTone::Soft, 0, 0, 100, 0, true, 0, 0, 0,
       1024, 0, 1024, 0, 35, false};
  CHECK(saveRecord(&store, "sys", "settings", 7, &v7, sizeof(v7)));
  SystemSettings s;
  s.animations = false;  // whatever the board says; the old record has no opinion
  bool migrated = false;
  CHECK(loadSettings(&store, s, &migrated) == RecordStatus::Ok);
  CHECK(migrated && s.volume == 35 && !s.autoUpdate && s.brightness == 140);
  CHECK(!s.animations);  // keeps the board default it started with
}
