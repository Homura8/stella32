// Settings > Sound & light by touch: the volume, the LED, Find my LED played by
// a person who tells the truth about what lit up, and the light-sensor
// calibration measured against the simulated sensor.
#include <string.h>

#include "sim_session.h"
#include "stella/ui/layout.h"
#include "test.h"

using namespace stella;
using namespace stella::host;

namespace {

void tapRect(SimSession &s, const Rect &r) { s.tap(r.centerX(), r.centerY()); }

// The rows of Sound & light, as SoundScreen::layout() lays them out.
Rect soundRow(SimSession &s, int i) {
  const Theme &t = s.os.theme();
  const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  const int want = t.m.control + t.dp(4), fit = (area.h - 2 * t.dp(4)) / 6, minimum = t.dp(32);
  const int rowH = want <= fit ? want : fit < minimum ? minimum : fit;
  return Rect(area.x, area.y + t.dp(4) + i * rowH, area.w, rowH);
}

// The i-th of n buttons in the bottom row of the two-part wizard screens.
Rect wizardButton(SimSession &s, int n, int i) {
  const Theme &t = s.os.theme();
  const Rect area = s.os.contentRect().inset(t.m.margin, t.m.gapS);
  const Rect row(area.x, area.bottom() - t.m.control, area.w, t.m.control);
  const int w = (row.w - t.m.gapS * (n - 1)) / n;
  return Rect(row.x + i * (w + t.m.gapS), row.y, i == n - 1 ? row.right() - (row.x + i * (w + t.m.gapS)) : w, row.h);
}

void openSoundAndLight(SimSession &s) {
  s.os.launch("settings");
  s.run(100);
  while (s.os.topScreen() != &s.settings.root()) s.os.pop();
  const Point row = s.settingsRow(2);
  s.tap(row.x, row.y);
  CHECK(s.os.topScreen()->title() && strcmp(s.os.topScreen()->title(), "Sound & light") == 0);
}

const char *title(SimSession &s) { return s.os.topScreen()->title() ? s.os.topScreen()->title() : ""; }

bool sameAsFullRedraw(SimSession &s) {
  const std::vector<Color> incremental = s.dev.display.framebuffer();
  s.os.invalidateAll();
  s.run(20);
  return incremental == s.dev.display.framebuffer();
}

// Plays the person in Find my LED against the simulated LED's real wiring.
void findLed(SimSession &s) {
  openSoundAndLight(s);
  tapRect(s, soundRow(s, 3));
  CHECK(strcmp(title(s), "Find my LED") == 0);
  tapRect(s, wizardButton(s, 1, 0));  // Start
  for (int guard = 0; guard < 30 && s.dev.led.probing(); ++guard) {
    int colour = 3;  // None
    for (int c = 0; c < 3; ++c)
      if (s.dev.led.probeLit(c)) colour = c;
    tapRect(s, wizardButton(s, 4, colour));
  }
}

}  // namespace

TEST(sound_and_light_screen_sets_volume_mode_and_level) {
  SimSession s(320, 240);
  openSoundAndLight(s);
  const Rect vol = soundRow(s, 0);
  const int before = s.dev.sound.tones();
  s.drag(vol.x + vol.w / 2, vol.centerY(), vol.right() - 30, vol.centerY());
  CHECK(s.os.settings().volume > 60);
  s.run(600);
  CHECK(s.dev.sound.tones() > before);  // let go: the chime
  // The LED mode segments: Off | Alerts | Glow.
  const Rect mode = soundRow(s, 1);
  s.tap(mode.right() - 20, mode.centerY());
  CHECK_EQ(s.os.settings().ledMode, 2);
  const int controlX = mode.x + s.os.theme().m.padding + s.os.theme().dp(96) + s.os.theme().m.gapM;  // the segments start after the label
  s.tap((controlX + mode.right()) / 2, mode.centerY());
  CHECK_EQ(s.os.settings().ledMode, 1);
  s.tap(controlX + 12, mode.centerY());
  CHECK_EQ(s.os.settings().ledMode, 0);
  // The level slider.
  const Rect level = soundRow(s, 2);
  s.drag(level.right() - 30, level.centerY(), level.x + level.w / 2 + 20, level.centerY());
  CHECK(s.os.settings().ledLevel < 90 && s.os.settings().ledLevel > 20);
}

TEST(auto_dim_cannot_be_switched_on_before_the_sensor_is_calibrated) {
  SimSession s(320, 240);
  openSoundAndLight(s);
  tapRect(s, soundRow(s, 4));
  s.run(200);
  CHECK(!s.os.settings().autoBrightness);
}

TEST(find_my_led_learns_the_standard_wiring) {
  SimSession s(320, 240);
  s.dev.led.truth = LedWiring{{4, 16, 17}, true};
  SystemSettings st = s.os.settings();  // start from a wrong guess
  st.ledPin[0] = 255;
  st.ledPin[1] = 255;
  st.ledPin[2] = 255;
  s.os.applySettings(st);
  findLed(s);
  CHECK(!s.dev.led.probing());
  tapRect(s, wizardButton(s, 2, 0));  // Save
  CHECK(s.os.settings().ledPin[0] == 4 && s.os.settings().ledPin[1] == 16 && s.os.settings().ledPin[2] == 17);
  CHECK(s.os.settings().ledInverted);
  CHECK(strcmp(title(s), "Sound & light") == 0);
  // And normal operation lights the right colours: an amber alert shows red + green.
  Alert a;
  a.title = "x";
  a.sound = true;
  s.os.showAlert(a);
  int redMax = 0, blueMax = 0;
  for (int i = 0; i < 100; ++i) {
    s.run(20);
    redMax = s.dev.led.shown(0) > redMax ? s.dev.led.shown(0) : redMax;
    blueMax = s.dev.led.shown(2) > blueMax ? s.dev.led.shown(2) : blueMax;
  }
  CHECK(redMax > 60 && blueMax < redMax / 2);
}

TEST(find_my_led_learns_a_shuffled_active_high_board) {
  SimSession s(320, 240);
  s.dev.led.truth = LedWiring{{17, 4, 16}, false};  // red 17, green 4, blue 16, lit by a high pin
  findLed(s);
  tapRect(s, wizardButton(s, 2, 0));
  const SystemSettings &st = s.os.settings();
  CHECK(st.ledPin[0] == 17 && st.ledPin[1] == 4 && st.ledPin[2] == 16);
  CHECK(!st.ledInverted);
  CHECK(s.dev.led.wiring.pin[0] == 17 && !s.dev.led.wiring.inverted);
}

TEST(find_my_led_handles_a_single_colour_led) {
  SimSession s(320, 240);
  s.dev.led.truth = LedWiring{{kNoPin, kNoPin, 17}, true};  // one blue LED on 17
  findLed(s);
  tapRect(s, wizardButton(s, 2, 0));
  const SystemSettings &st = s.os.settings();
  CHECK(st.ledPin[0] == 255 && st.ledPin[1] == 255 && st.ledPin[2] == 17 && st.ledInverted);
}

TEST(find_my_led_reports_when_nothing_lights_and_changes_nothing) {
  SimSession s(320, 240);
  s.dev.led.truth = LedWiring{{kNoPin, kNoPin, kNoPin}, true};
  const SystemSettings before = s.os.settings();
  findLed(s);
  CHECK(!s.dev.led.probing());
  // Not found: one button, "Try again". It restarts the probing.
  tapRect(s, wizardButton(s, 1, 0));
  CHECK(s.dev.led.probing());
  s.os.pop();  // leaving ends the probe
  CHECK(!s.dev.led.probing());
  CHECK(memcmp(before.ledPin, s.os.settings().ledPin, 3) == 0 && before.ledInverted == s.os.settings().ledInverted);
}

TEST(calibration_measures_the_sensor_in_its_own_case_and_turns_dimming_on) {
  SimSession s(320, 240, kMorningEpoch);
  s.dev.light.ambient = 0.f;  // a dark room
  openSoundAndLight(s);
  tapRect(s, soundRow(s, 5));
  CHECK(strcmp(title(s), "Light sensor") == 0);
  tapRect(s, wizardButton(s, 1, 0));  // Start
  bool wentDark = false;
  for (int i = 0; i < 90; ++i) {  // about 6 s
    s.run(100);
    wentDark |= s.dev.display.backlight() == 0;
  }
  CHECK(wentDark);  // the screen's own light was measured too
  s.dev.light.ambient = 1.f;  // the person turns the lights on
  tapRect(s, wizardButton(s, 1, 0));  // Measure
  s.run(3000);
  const SystemSettings &st = s.os.settings();
  CHECK(st.lightCalibrated() && st.autoBrightness);
  CHECK(st.lightDark > 680 && st.lightDark < 720);          // the sim's dark reading is 700
  CHECK(st.lightSelf > 400 && st.lightSelf < 440);          // its backlight adds 420
  CHECK(st.lightBright > 2870 && st.lightBright < 2930);    // 700 + 2200
  tapRect(s, wizardButton(s, 1, 0));  // Close
  CHECK(strcmp(title(s), "Sound & light") == 0);
  // And it works: a dark room now dims.
  s.dev.light.ambient = 1.f;
  s.run(8000);
  const int bright = s.dev.display.backlight();
  s.dev.light.ambient = 0.f;
  s.run(20000);
  CHECK(s.dev.display.backlight() < bright / 3);
}

TEST(calibration_fails_politely_when_the_sensor_never_changes) {
  SimSession s(320, 240, kMorningEpoch);
  s.dev.light.range = 5;  // covered by a thick case: nothing changes
  openSoundAndLight(s);
  tapRect(s, soundRow(s, 5));
  tapRect(s, wizardButton(s, 1, 0));
  s.run(7000);
  tapRect(s, wizardButton(s, 1, 0));  // Measure
  s.run(3000);
  CHECK(!s.os.settings().lightCalibrated() && !s.os.settings().autoBrightness);
  // The screen is back to normal and the person can try again.
  CHECK(s.dev.display.backlight() > 20);
  tapRect(s, wizardButton(s, 1, 0));  // Try again
  s.run(500);
  CHECK(s.dev.display.backlight() == 0 || s.dev.display.backlight() > 0);
  s.os.pop();
  s.run(300);
  CHECK(s.dev.display.backlight() > 20);  // leaving mid-measurement gives the screen back
}

TEST(calibration_handles_a_sensor_that_reads_lower_in_light) {
  SimSession s(320, 240, kMorningEpoch);
  s.dev.light.inverted = true;
  s.dev.light.dark = 3400;
  s.dev.light.ambient = 0.f;
  openSoundAndLight(s);
  tapRect(s, soundRow(s, 5));
  tapRect(s, wizardButton(s, 1, 0));
  s.run(7000);
  s.dev.light.ambient = 1.f;
  tapRect(s, wizardButton(s, 1, 0));
  s.run(3000);
  const SystemSettings &st = s.os.settings();
  CHECK(st.lightCalibrated() && st.lightBright < st.lightDark);
  CHECK(st.lightSelf < 0);
}

TEST(the_new_screens_render_at_every_size_and_incrementally) {
  const int sizes[][2] = {{240, 320}, {320, 240}, {480, 320}, {800, 480}};
  for (const auto &sz : sizes) {
    SimSession s(sz[0], sz[1], kMorningEpoch);
    openSoundAndLight(s);
    CHECK(soundRow(s, 5).bottom() <= sz[1]);
    CHECK(soundRow(s, 0).h >= s.os.theme().dp(32));
    CHECK(sameAsFullRedraw(s));
    tapRect(s, soundRow(s, 3));
    CHECK(strcmp(title(s), "Find my LED") == 0);
    CHECK(wizardButton(s, 1, 0).bottom() <= sz[1]);
    CHECK(sameAsFullRedraw(s));
    tapRect(s, wizardButton(s, 1, 0));  // Start: four answer buttons
    CHECK(wizardButton(s, 4, 3).h >= s.os.theme().dp(32));
    CHECK(sameAsFullRedraw(s));
    s.os.pop();
    tapRect(s, soundRow(s, 5));
    CHECK(strcmp(title(s), "Light sensor") == 0);
    CHECK(sameAsFullRedraw(s));
    s.os.pop();
  }
}
